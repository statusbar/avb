// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

//
// PTP Wake Example - Demonstrates precision wake timing using PTP clock
//
// Usage: ptpclient_wake_example --ptp.driver=<name> --ptp.device=<path> --ptp.period=<ns> [options]
//
// Requires CAP_SYS_NICE capability or root for realtime thread priority.

#include "statusbar/config/config.hpp"
#include "statusbar/ptpclient/ptpclient.hpp"
#include "statusbar/realtime/realtime.hpp"
#include "statusbar/status/status.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iterator>
#include <print>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

namespace {

// Config contains PTP fields via composition
struct Config
{
    statusbar::ptpclient::PtpAppConfig ptp_app;
    // Add any example-specific fields here if needed
};

// Use realtime module's shutdown handling

auto build_arg_specs(Config& config) -> statusbar::args::ArgumentSpecs
{
    statusbar::args::ArgumentSpecs specs;

    // Add PTP options with lambda bindings to config
    statusbar::ptpclient::add_ptp_arg_specs(specs, config.ptp_app);

    return specs;
}

void print_usage(char const* program_name, statusbar::args::ArgumentSpecs const& specs)
{
    static constexpr std::array<std::string_view, 3> examples{
        "--ptp.driver=linuxptp --ptp.device=/dev/ptp0 --ptp.period=1000000",
        "--ptp.period=1000000 --ptp.compensation=-3000   # Wake 3us earlier",
        "--ptp.period=1000000 --ptp.no-rt                # Without RT priority",
    };
    statusbar::config::default_print_usage(program_name, specs, "", examples);
    std::print(stderr, "\nConfiguration file example:\n");
    std::print(stderr, "  [ptp]\n");
    std::print(stderr, "  driver = \"linuxptp\"\n");
    std::print(stderr, "  device = \"/dev/ptp0\"\n");
    std::print(stderr, "  period = 1000000\n");
    std::print(stderr, "  compensation = -3000\n");
    std::print(stderr, "  cpu = 3\n");
    std::print(stderr, "  no-rt = false\n");
    std::print(stderr, "\nPress Ctrl-C to stop and display statistics.\n");
}

}  // namespace

auto main(int argc, char** argv) -> int
{
    using namespace statusbar::ptpclient;

    // Build config and specs with bindings
    Config config;
    auto specs = build_arg_specs(config);

    // Parse CLI arguments, handle --completion, --help, --config-load, --config-save, apply bindings
    auto cli_result = statusbar::config::parse_cli_args(argc, argv, specs, print_usage);
    if (!cli_result) {
        return statusbar::config::handled_builtin_command(cli_result) ? 0 : 1;
    }

    // Validate period
    if (config.ptp_app.period_ns <= 0) {
        std::print(stderr, "Error: --ptp.period must be positive\n");
        return EXIT_FAILURE;
    }

    // Print configuration summary
    std::string config_summary;
    format_ptp_config_to(std::back_inserter(config_summary), config.ptp_app, "PTP Wake Example");
    std::print("{}", config_summary);

    // Set up signal handlers for clean shutdown
    statusbar::realtime::setup_shutdown_signal_handlers();

    // Setup PTP application (client, bridge, sampling, wait for healthy)
    auto is_shutdown = [] { return statusbar::realtime::is_shutdown_requested(); };
    auto ctx_result = setup_ptp_app(config.ptp_app, is_shutdown);

    if (!ctx_result) {
        if (statusbar::realtime::is_shutdown_requested()) {
            std::print("Shutdown requested during setup\n");
            return EXIT_SUCCESS;
        }
        std::print(stderr, "Error: Failed to setup PTP: {}\n", ctx_result.error().message());
        return EXIT_FAILURE;
    }

    auto& ctx = *ctx_result;

    std::print("\nStarting wake loop (Ctrl-C to stop)...\n\n");

    // Create PTP timer with callback
    auto timer = make_ptp_timer(
        *ctx.bridge,
        ctx.period_ns,
        [](statusbar::StatusValue<TimerWakeInfo> const& wake_info) {
            if (!wake_info) {
                std::print(stderr, "Warning: Timer error: {}\n", wake_info.error().message());
                return;
            }

            // Print periodic status (every 10000 wakes)
            if (wake_info->wake_count % 10000 == 0) {
                std::print(
                    "Wakes: {:8}  Error: {:+8} ns ({:+.3f} us)\n",
                    wake_info->wake_count,
                    wake_info->error_ns,
                    static_cast<double>(wake_info->error_ns) / 1000.0);
            }
        },
        ctx.compensation_ns,
        ctx.enable_realtime,
        ctx.cpu_affinity,
        ctx.threshold);

    // Start timer
    auto start_result = timer.start();
    if (!start_result) {
        std::print(stderr, "Error: Failed to start timer: {}\n", start_result.error().message());
        return EXIT_FAILURE;
    }

    // Wait for shutdown signal
    while (!statusbar::realtime::is_shutdown_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Clean shutdown
    std::print("\n\nShutting down...\n");
    timer.stop();
    std::print("\n\nguard stopping...\n");
    ctx.guard.stop();
    std::print("\n\nguard stopped...\n");

    // Print final statistics
    std::print("\n");
    std::string report;
    timer.stats().format_report_to(std::back_inserter(report), ctx.compensation_ns);
    std::print("{}", report);

    return EXIT_SUCCESS;
}