// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// AVB Entity Stereo I/O Tool - Runs an AVB entity with stereo audio passthrough and DSP
//
// Usage: avb_entity_stereo_io_tool [options]
//
// On Linux with hardware PTP:
//   avb_entity_stereo_io_tool --ptp.driver=linuxptp --ptp.device=/dev/ptp0
//
// On any platform (uses system clock):
//   avb_entity_stereo_io_tool --ptp.driver=system
//
// This tool instantiates AvbEntityStereoIO which provides:
// - One stereo 48 kHz AM824 listener stream (input)
// - One stereo 48 kHz AM824 talker stream (output)
// - Configurable DSP biquad filter (peak EQ)
// - Full ATDECC support (ADP, ACMP, AECP/AEM)
// - gPTP synchronization
// - MVRP/MSRP stream reservation

#include "statusbar/avb_entity/avb_entity_stereo_io.hpp"
#include "statusbar/config/config.hpp"
#include "statusbar/gptp/gptp_format.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/itc/itc_stop_token.hpp"
#include "statusbar/nanoavb/nanoavb.hpp"
#include "statusbar/net/net_message_reactor.hpp"
#include "statusbar/ptpclient/ptpclient.hpp"
#include "statusbar/realtime/realtime.hpp"
#include "statusbar/sm/sm.hpp"
#include "statusbar/status/status.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <functional>
#include <iterator>
#include <memory>
#include <print>
#include <string>
#include <system_error>

namespace {

using namespace statusbar;
using TimePoint = sm::TimePoint;

// Default network interface
#if defined(__APPLE__)
constexpr char const* DEFAULT_INTERFACE = "en0";
#else
constexpr char const* DEFAULT_INTERFACE = "eth0";
#endif

//
// Configuration
//

struct Config
{
    // PTP timer configuration
    ptpclient::PtpAppConfig ptp_app;

    // AVB Entity configuration
    avb_entity::AvbEntityStereoIOConfig entity{
        .entity_id = ieee::Eui64{0x70, 0xB3, 0xD5, 0xED, 0xCF, 0x00, 0x00, 0x01},
        .entity_model_id = ieee::Eui64{0x70, 0xB3, 0xD5, 0xED, 0xCF, 0x00, 0x00, 0x00},
        .interface_name = DEFAULT_INTERFACE,
        .talker_dest_mac = {0x91, 0xE0, 0xF0, 0x00, 0xFE, 0x00},
        .vlan_id = 2,
        .filter_freq_hz = 1000.0,
        .filter_gain_db = 0.0,  // Unity gain by default
        .filter_q = 0.707,      // Butterworth Q
        .entity_name = "AVB Stereo IO Tool",
        .firmware_version = "1.0.0",
    };

    // Runtime options
    bool verbose{true};
    bool dump_stats_on_exit{true};
};

//
// Argument Specifications
//

auto build_arg_specs(Config& config) -> args::ArgumentSpecs
{
    args::ArgumentSpecs specs;

    // Add PTP options
    ptpclient::add_ptp_arg_specs(specs, config.ptp_app);

    // Entity configuration - use native EUI types
    specs.add<ieee::Eui64>(
        "entity.id", "Entity ID (EUI-64)", config.entity.entity_id, [&](auto v) { config.entity.entity_id = v; });

    specs.add<ieee::Eui64>("entity.model_id", "Entity Model ID (EUI-64)", config.entity.entity_model_id, [&](auto v) {
        config.entity.entity_model_id = v;
    });

    specs.add_device(
        "interface", "Network interface", DEFAULT_INTERFACE, [&](auto v) { config.entity.interface_name = std::string{v}; });

    specs.add<std::string>("entity.name", "Entity name (shown in ATDECC controllers)", config.entity.entity_name, [&](auto v) {
        config.entity.entity_name = v;
    });

    specs.add<uint16_t>("vlan_id", "VLAN ID for AVB streams", config.entity.vlan_id, [&](auto v) { config.entity.vlan_id = v; });

    specs.add<ieee::Eui48>("talker.dest_mac", "Talker destination multicast MAC", config.entity.talker_dest_mac, [&](auto v) {
        config.entity.talker_dest_mac = v;
    });

    // DSP filter configuration
    specs.add<double>("filter.freq_hz", "Filter center frequency (Hz)", config.entity.filter_freq_hz, [&](auto v) {
        config.entity.filter_freq_hz = v;
    });

    specs.add<double>(
        "filter.gain_db", "Filter gain (dB, negative=cut, positive=boost)", config.entity.filter_gain_db, [&](auto v) {
            config.entity.filter_gain_db = v;
        });

    specs.add<double>("filter.q", "Filter Q factor", config.entity.filter_q, [&](auto v) { config.entity.filter_q = v; });

    // Runtime options
    specs.add<bool>("verbose", "Enable verbose output", config.verbose, [&](auto v) { config.verbose = v; });
    specs.add<bool>("dump_stats_on_exit", "Dump timer statistics on exit", config.dump_stats_on_exit, [&](auto v) {
        config.dump_stats_on_exit = v;
    });

    return specs;
}

//
// Help and Usage
//

void print_usage(char const* program_name, args::ArgumentSpecs const& specs)
{
    std::print(stderr, "Usage: {} [options]\n", program_name);
    std::print(stderr, "\nAVB Entity Stereo I/O Tool\n");
    std::print(stderr, "Runs an AVB entity with stereo audio passthrough and DSP processing.\n");
    std::print(stderr, "\nOptions:\n");

    std::string help;
    specs.format_help_to(std::back_inserter(help));
    std::print(stderr, "{}", help);

    std::print(stderr, "\nExamples:\n");
#if defined(__linux__)
    std::print(stderr, "  {} --ptp.driver=linuxptp --ptp.device=/dev/ptp0\n", program_name);
    std::print(stderr, "  {} --interface=eth0 --filter.gain_db=-6\n", program_name);
#else
    std::print(stderr, "  {} --interface=en0 --filter.gain_db=-6\n", program_name);
#endif
    std::print(stderr, "  {} --ptp.driver=system\n", program_name);
    std::print(stderr, "\nPress Ctrl-C to stop.\n");
}

//
// Configuration Display
//

void print_entity_config(Config const& config)
{
    std::print("AVB Entity Stereo I/O Tool\n");
    std::print("==========================\n");
    std::print("Entity ID:        {}\n", ieee::to_string(config.entity.entity_id));
    std::print("Entity Model ID:  {}\n", ieee::to_string(config.entity.entity_model_id));
    std::print("Entity Name:      {}\n", config.entity.entity_name);
    std::print("Interface:        {}\n", config.entity.interface_name);
    std::print("VLAN ID:          {}\n", config.entity.vlan_id);
    std::print("Talker Dest MAC:  {}\n", ieee::to_string(config.entity.talker_dest_mac));
    std::print("\nDSP Filter:\n");
    std::print("  Frequency:      {:.1f} Hz\n", config.entity.filter_freq_hz);
    std::print("  Gain:           {:.1f} dB\n", config.entity.filter_gain_db);
    std::print("  Q:              {:.3f}\n", config.entity.filter_q);
    std::print("\nAudio Format:\n");
    std::print("  Sample Rate:    {} Hz\n", avb_entity::AvbEntityStereoIO::SAMPLE_RATE);
    std::print("  Channels:       {} (stereo)\n", avb_entity::AvbEntityStereoIO::CHANNELS);
    std::print("  Samples/Packet: {}\n", avb_entity::AvbEntityStereoIO::SAMPLES_PER_PACKET);
}

//
// Main Loop Result
//

struct MainLoopResult
{
    int exit_code{EXIT_SUCCESS};
    stats::AtomicWakeStats::Snapshot wake_stats{};
    int64_t compensation_ns{0};
    int64_t recovery_count{0};
    int64_t missed_cycles{0};
};

//
// Main Loop with PTP Timer
//

MainLoopResult run_main_loop(
    net::MessageReactor& reactor, ptpclient::PtpAppContext& ctx, avb_entity::AvbEntityStereoIO& entity, Config const& config)
{
    MainLoopResult result;
    result.compensation_ns = ctx.compensation_ns;

    // Packet rate timer period: 6 samples @ 48 kHz = 125 µs
    constexpr int64_t PACKET_PERIOD_NS = 125'000;

    // Track gPTP announce status
    auto* net_handlers = entity.net_handlers();
    bool had_grandmaster = net_handlers ? net_handlers->gptp_handler().has_grandmaster() : false;

    // Create PTP timer for realtime audio packet handling
    auto timer = ptpclient::make_ptp_timer(
        *ctx.bridge,
        PACKET_PERIOD_NS,
        [&](StatusValue<ptpclient::TimerWakeInfo> const& wake_info) {
            if (!wake_info) {
                std::print(stderr, "Warning: Timer error: {}\n", wake_info.error().message());
                return;
            }

            auto const now = TimePoint{std::chrono::nanoseconds{wake_info->actual_time_ns}};

            // Check for gPTP grandmaster changes
            if (net_handlers) {
                bool const has_gm = net_handlers->gptp_handler().has_grandmaster();
                if (has_gm && !had_grandmaster) {
                    entity.on_gptp_announce(now, true);
                }
                had_grandmaster = has_gm;
            }

            // Process audio (DSP and packet handling)
            entity.process_audio(now);

            // Print periodic status
            if (config.verbose && wake_info->wake_count % 8000 == 0) {
                std::print(
                    "Wake: {:8}  Error: {:+6} ns  State: {}\n", wake_info->wake_count, wake_info->error_ns, entity.state_string());
            }
        },
        ctx.compensation_ns,
        ctx.enable_realtime,
        ctx.cpu_affinity);

    // Start PTP timer
    auto start_result = timer.start();
    if (!start_result) {
        std::print(stderr, "Error: Failed to start timer: {}\n", start_result.error().message());
        result.exit_code = EXIT_FAILURE;
        return result;
    }

    // Simulate link up on startup
    auto startup_time = TimePoint{std::chrono::steady_clock::now().time_since_epoch()};
    entity.on_link_up(startup_time);

    // Watchdog timeout configuration
    constexpr auto GPTP_LOCK_TIMEOUT = std::chrono::seconds{10};
    constexpr auto VLAN_REGISTER_TIMEOUT = std::chrono::seconds{5};
    auto last_state_change_time = std::chrono::steady_clock::now();
    std::string last_state = entity.state_string();

    // Wire up gPTP announce callback to update ADP advertiser
    if (net_handlers) {
        net_handlers->gptp_handler().set_callbacks(
            nanoavb::GptpAnnounceCallbacks{
                .grandmaster_id_changed =
                    [&entity](int64_t now_ns, gptp::ClockIdentity const& grandmaster_id, gptp::AnnounceMessage const& announce) {
                        std::string gm_str;
                        gptp::format_to(std::back_inserter(gm_str), grandmaster_id);
                        std::print(
                            "gPTP: Grandmaster changed to {} (priority1={}, priority2={})\n",
                            gm_str,
                            announce.grandmaster_priority1.get(),
                            announce.grandmaster_priority2.get());

                        // Update ADP advertiser with new grandmaster info
                        entity.components().adp_advertiser.set_gptp_info(grandmaster_id, 0);
                        entity.components().adp_advertiser.notify_entity_changed();

                        // Notify entity state machine
                        (void)now_ns;
                        auto const sm_now = std::chrono::steady_clock::now();
                        entity.on_gptp_announce(sm_now, true);
                    }});
    }

    // Poll reactor for network I/O until shutdown
    while (!realtime::is_shutdown_requested()) {
        (void)reactor.poll_once(100);

        // Check for state changes to reset watchdog
        auto const current_state = entity.state_string();
        if (current_state != last_state) {
            if (config.verbose) {
                std::print("State: {} -> {}\n", last_state, current_state);
            }
            last_state_change_time = std::chrono::steady_clock::now();
            last_state = current_state;
        }

        // Check for watchdog timeout in waiting states
        auto const now = std::chrono::steady_clock::now();
        auto const time_in_state = now - last_state_change_time;
        auto const sm_now = TimePoint{now.time_since_epoch()};

        if (current_state == "Init" && time_in_state > GPTP_LOCK_TIMEOUT) {
            entity.on_timeout(sm_now);
            last_state_change_time = now;
        } else if (current_state == "WaitVlanBase" && time_in_state > VLAN_REGISTER_TIMEOUT) {
            entity.on_timeout(sm_now);
            last_state_change_time = now;
        }
    }

    // Stop timer and capture statistics
    timer.stop();
    result.wake_stats = timer.stats();
    result.recovery_count = timer.recovery_count();
    result.missed_cycles = timer.missed_cycles();
    ctx.guard.stop();

    return result;
}

//
// Final Status Display
//

void print_final_status(avb_entity::AvbEntityStereoIO const& entity, MainLoopResult const& result, bool dump_stats)
{
    std::print("\n=== Final Status ===\n");
    std::print("  Entity State:    {}\n", entity.state_string());
    std::print("  Timer Recovery:  {}\n", result.recovery_count);
    std::print("  Missed Cycles:   {}\n", result.missed_cycles);
    entity.print_state();
    std::print("====================\n");

    if (dump_stats) {
        std::string stats_report;
        result.wake_stats.format_report_to(std::back_inserter(stats_report), result.compensation_ns);
        std::print("{}", stats_report);
    }
}

}  // namespace

//
// Main Entry Point
//

auto main(int argc, char** argv) -> int
{
    using namespace statusbar;

    // Build config and specs with bindings
    Config config;
    auto specs = build_arg_specs(config);

    // Parse CLI arguments
    auto cli_result = config::parse_cli_args(argc, argv, specs, print_usage, "statusbar-avb-stereo-io");
    if (!cli_result) {
        return config::handled_builtin_command(cli_result) ? 0 : 1;
    }

    // Set up signal handlers for clean shutdown
    realtime::setup_shutdown_signal_handlers();

    // Print configuration
    print_entity_config(config);

    // Print PTP configuration
    std::string ptp_summary;
    ptpclient::format_ptp_config_to(std::back_inserter(ptp_summary), config.ptp_app, "\nPTP Configuration");
    std::print("{}\n", ptp_summary);

    // Create the AVB entity
    avb_entity::AvbEntityStereoIO entity{config.entity};

    // Create message reactor with shutdown flag and monotonic clock
    itc::StopToken shutdown_flag{};
    net::MessageReactor reactor{shutdown_flag, net::monotonic_ns, 10};

    // Start the entity (adds handlers to reactor)
    auto start_result = entity.start(reactor);
    if (!start_result) {
        std::print(stderr, "Error: Failed to start entity: {}\n", start_result.error().message());
        return EXIT_FAILURE;
    }

    std::print("\nEntity started. Network handlers registered.\n");

    // Setup PTP application with automatic fallback to system clock
    auto is_shutdown = [] { return realtime::is_shutdown_requested(); };
    auto ctx_result = ptpclient::setup_ptp_app_with_fallback(config.ptp_app, is_shutdown);

    if (!ctx_result) {
        if (realtime::is_shutdown_requested()) {
            std::print("Shutdown requested during setup\n");
            (void)entity.stop();
            return EXIT_SUCCESS;
        }
        std::print(stderr, "Error: Failed to setup PTP: {}\n", ctx_result.error().message());
        (void)entity.stop();
        return EXIT_FAILURE;
    }

    auto& ctx = *ctx_result;

    std::print("\nStarting main loop (Ctrl-C to stop)...\n\n");

    // Run main loop with PTP timer
    auto loop_result = run_main_loop(reactor, ctx, entity, config);

    // Clean shutdown
    std::print("\n\nShutting down...\n");

    // Notify entity of shutdown
    auto stop_time = TimePoint{std::chrono::steady_clock::now().time_since_epoch()};
    entity.on_link_down(stop_time);

    // Stop entity
    (void)entity.stop();
    std::print("Entity stopped.\n");

    // Print final status
    print_final_status(entity, loop_result, config.dump_stats_on_exit);

    return loop_result.exit_code;
}