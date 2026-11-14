// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// CLI tool to read and dump .aem descriptor storage blobs via DescriptorStorage.
/// Used for cross-language validation with Python AEMXML tooling.

#include "statusbar/atdecc/atdecc_aem_descriptor.hpp"
#include "statusbar/atdecc/atdecc_descriptor_storage.hpp"
#include "statusbar/buffer/stream_utils.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <print>
#include <span>
#include <string_view>
#include <vector>

using namespace statusbar::atdecc::aem;

namespace {

// Every descriptor type the storage may hold, in the order they are reported.
constexpr uint16_t TYPES_TO_TRY[] = {
    DESCRIPTOR_ENTITY,
    DESCRIPTOR_CONFIGURATION,
    DESCRIPTOR_AUDIO_UNIT,
    DESCRIPTOR_VIDEO_UNIT,
    DESCRIPTOR_SENSOR_UNIT,
    DESCRIPTOR_STREAM_INPUT,
    DESCRIPTOR_STREAM_OUTPUT,
    DESCRIPTOR_JACK_INPUT,
    DESCRIPTOR_JACK_OUTPUT,
    DESCRIPTOR_AVB_INTERFACE,
    DESCRIPTOR_CLOCK_SOURCE,
    DESCRIPTOR_MEMORY_OBJECT,
    DESCRIPTOR_LOCALE,
    DESCRIPTOR_STRINGS,
    DESCRIPTOR_STREAM_PORT_INPUT,
    DESCRIPTOR_STREAM_PORT_OUTPUT,
    DESCRIPTOR_EXTERNAL_PORT_INPUT,
    DESCRIPTOR_EXTERNAL_PORT_OUTPUT,
    DESCRIPTOR_INTERNAL_PORT_INPUT,
    DESCRIPTOR_INTERNAL_PORT_OUTPUT,
    DESCRIPTOR_AUDIO_CLUSTER,
    DESCRIPTOR_VIDEO_CLUSTER,
    DESCRIPTOR_SENSOR_CLUSTER,
    DESCRIPTOR_AUDIO_MAP,
    DESCRIPTOR_VIDEO_MAP,
    DESCRIPTOR_SENSOR_MAP,
    DESCRIPTOR_CONTROL,
    DESCRIPTOR_SIGNAL_SELECTOR,
    DESCRIPTOR_MIXER,
    DESCRIPTOR_MATRIX,
    DESCRIPTOR_MATRIX_SIGNAL,
    DESCRIPTOR_SIGNAL_SPLITTER,
    DESCRIPTOR_SIGNAL_COMBINER,
    DESCRIPTOR_SIGNAL_DEMULTIPLEXER,
    DESCRIPTOR_SIGNAL_MULTIPLEXER,
    DESCRIPTOR_SIGNAL_TRANSCODER,
    DESCRIPTOR_CLOCK_DOMAIN,
    DESCRIPTOR_CONTROL_BLOCK,
    DESCRIPTOR_TIMING,
    DESCRIPTOR_PTP_INSTANCE,
    DESCRIPTOR_PTP_PORT,
};

auto wants_help(int argc, char** argv) -> bool
{
    for (int i = 1; i < argc; ++i) {
        std::string_view const a = argv[i];
        if (a == "--help" || a == "-h") {
            return true;
        }
    }
    return false;
}

void print_usage(FILE* out, char const* prog)
{
    std::println(out, "Usage: {} <file.aem>", prog);
    std::println(out, "");
    std::println(out, "Dump an AEM DescriptorStorage blob (as statusbar-aemxml json2bin writes it):");
    std::println(out, "the configuration count, every descriptor by configuration, type and index");
    std::println(out, "with its size, and every string symbol. --help prints this.");
}

auto read_file(char const* path) -> std::optional<std::vector<uint8_t>>
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::println(stderr, "Error: cannot open '{}'", path);
        return std::nullopt;
    }
    std::vector<uint8_t> data(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    statusbar::stream_read(file, data);
    return data;
}

void dump_descriptors(DescriptorStorage const& storage, uint16_t const config_count)
{
    // Entity descriptor is always config 0
    auto entity_desc = storage.get_descriptor(0, DESCRIPTOR_ENTITY, 0);
    if (entity_desc.has_value()) {
        std::println("DESCRIPTOR config=0 type=0x{:04x} index=0 size={}", DESCRIPTOR_ENTITY, entity_desc->size());
    }
    for (uint16_t cfg = 0; cfg < config_count; ++cfg) {
        for (auto const type : TYPES_TO_TRY) {
            if (type == DESCRIPTOR_ENTITY) {
                continue;  // already handled
            }
            for (uint16_t idx = 0; idx < 65535; ++idx) {
                auto desc = storage.get_descriptor(cfg, type, idx);
                if (!desc.has_value()) {
                    break;  // no more of this type
                }
                std::println("DESCRIPTOR config={} type=0x{:04x} index={} size={}", cfg, type, idx, desc->size());
            }
        }
    }
}

void dump_symbols(DescriptorStorage const& storage, uint16_t const config_count)
{
    for (uint16_t cfg = 0; cfg < config_count; ++cfg) {
        for (auto const type : TYPES_TO_TRY) {
            for (uint16_t idx = 0; idx < 65535; ++idx) {
                auto sym = storage.get_symbol(cfg, type, idx);
                if (!sym.has_value()) {
                    break;
                }
                std::println("SYMBOL config={} type=0x{:04x} index={} code=0x{:08x}", cfg, type, idx, *sym);
            }
        }
    }
}

void print_header(size_t const blob_size, uint16_t const config_count)
{
    std::println("DESCRIPTOR_STORAGE");
    std::println("blob_size={}", blob_size);
    std::println("config_count={}", config_count);
}

void dump_storage(DescriptorStorage const& storage, size_t const blob_size)
{
    auto const config_count = storage.get_configuration_count();
    print_header(blob_size, config_count);
    dump_descriptors(storage, config_count);
    dump_symbols(storage, config_count);
    std::println("END");
}

auto dump_file(char const* path) -> int
{
    auto const data = read_file(path);
    if (!data) {
        return 1;
    }
    auto result = DescriptorStorage::create(std::span<uint8_t const>(*data));
    if (!result.has_value()) {
        std::println(stderr, "Error: invalid descriptor storage blob");
        return 1;
    }
    dump_storage(result.value(), data->size());
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    if (wants_help(argc, argv)) {
        print_usage(stdout, argv[0]);
        return 0;
    }
    if (argc != 2) {
        print_usage(stderr, argv[0]);
        return 1;
    }
    return dump_file(argv[1]);
}
