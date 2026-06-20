// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/config/config.hpp"
#include "statusbar/netdump/netdump.hpp"
#include "statusbar/pcap/pcap.hpp"
#include "statusbar/status/status.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <iterator>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

struct Config
{
    std::string file;
};

void print_frame(uint64_t timestamp_us, std::span<uint8_t const> frame)
{
    // Format timestamp as seconds.microseconds
    uint64_t const seconds = timestamp_us / 1000000;
    uint64_t const microseconds = timestamp_us % 1000000;

    std::print("{}.{:06}: ", seconds, microseconds);

    std::string output;
    statusbar::netdump::format_frame(std::back_inserter(output), frame);
    std::print("{}", output);
}

/// Detect file format by reading magic number
/// @return true if pcapng, false if pcap
bool is_pcapng_file(std::string const& filename)
{
    FILE* f = std::fopen(filename.c_str(), "rb");
    if (!f) {
        return false;
    }

    uint32_t magic = 0;
    bool result = false;
    if (std::fread(&magic, sizeof(magic), 1, f) == 1) {
        // PcapNG SHB block type is 0x0a0d0d0a (same in both endians due to symmetry)
        result = (magic == 0x0a0d0d0a);
    }
    std::fclose(f);
    return result;
}

template <typename Reader>
int dump_packets(std::string const& filename)
{
    auto reader_result = Reader::open(filename);
    if (!reader_result) {
        std::print(stderr, "Error: open '{}': {}\n", filename, reader_result.error().message());
        return EXIT_FAILURE;
    }
    auto& reader = *reader_result;
    statusbar::pcap::Packet packet;
    uint64_t timestamp_us = 0;

    for (;;) {
        auto step = reader.read_packet(&timestamp_us, packet);
        if (!step) {
            std::print(stderr, "Error: read failed: {}\n", step.error().message());
            return EXIT_FAILURE;
        }
        if (!*step) {
            break;
        }
        print_frame(timestamp_us, packet);
        std::print("\n");
    }
    return EXIT_SUCCESS;
}

auto build_arg_specs(Config& config) -> statusbar::args::ArgumentSpecs
{
    statusbar::args::ArgumentSpecs specs;

    specs.add_file("file", "Path to PCAP or PCAPng file", "", [&](auto v) { config.file = std::string{v}; });

    return specs;
}

void print_usage(char const* program_name, statusbar::args::ArgumentSpecs const& specs)
{
    static constexpr std::array<std::string_view, 2> examples{
        "--file=capture.pcap",
        "--file=capture.pcapng",
    };
    statusbar::config::default_print_usage(
        program_name, specs, "Reads a PCAP or PCAPng file given by --file (required).", examples);
}

}  // namespace

auto main(int argc, char** argv) -> int
{
    // Build config and specs with bindings
    Config config;
    auto specs = build_arg_specs(config);

    // Parse CLI arguments, handle --completion, --help, --config-load, --config-save, apply bindings
    auto cli_result = statusbar::config::parse_cli_args(argc, argv, specs, print_usage, "statusbar-pcap-dump");
    if (!cli_result) {
        return statusbar::config::handled_builtin_command(cli_result) ? 0 : 1;
    }

    // Validate required options
    if (config.file.empty()) {
        std::print(stderr, "Error: --file is required\n\n");
        print_usage(argv[0], specs);
        return EXIT_FAILURE;
    }

    if (is_pcapng_file(config.file)) {
        return dump_packets<statusbar::pcap::PcapngReader>(config.file);
    }
    return dump_packets<statusbar::pcap::FileReader>(config.file);
}