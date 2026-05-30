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
#include <print>
#include <vector>

using namespace statusbar::atdecc::aem;

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::println(stderr, "Usage: {} <file.aem>", argv[0]);
        return 1;
    }

    // Read file into memory
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file) {
        std::println(stderr, "Error: cannot open '{}'", argv[1]);
        return 1;
    }
    auto const size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    statusbar::stream_read(file, data);

    // Create DescriptorStorage
    auto result = DescriptorStorage::create(std::span<uint8_t const>(data));
    if (!result.has_value()) {
        std::println(stderr, "Error: invalid descriptor storage blob");
        return 1;
    }
    auto const& storage = result.value();

    auto const config_count = storage.get_configuration_count();
    std::println("DESCRIPTOR_STORAGE");
    std::println("blob_size={}", data.size());
    std::println("config_count={}", config_count);

    // Enumerate all descriptors by trying known types
    static constexpr uint16_t types_to_try[] = {
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

    // Entity descriptor is always config 0
    auto entity_desc = storage.get_descriptor(0, DESCRIPTOR_ENTITY, 0);
    if (entity_desc.has_value()) {
        std::println("DESCRIPTOR config=0 type=0x{:04x} index=0 size={}", DESCRIPTOR_ENTITY, entity_desc->size());
    }

    // Per-configuration descriptors
    for (uint16_t cfg = 0; cfg < config_count; ++cfg) {
        for (auto const type : types_to_try) {
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

    // Dump symbols
    for (uint16_t cfg = 0; cfg < config_count; ++cfg) {
        for (auto const type : types_to_try) {
            for (uint16_t idx = 0; idx < 65535; ++idx) {
                auto sym = storage.get_symbol(cfg, type, idx);
                if (!sym.has_value()) {
                    break;
                }
                std::println("SYMBOL config={} type=0x{:04x} index={} code=0x{:08x}", cfg, type, idx, *sym);
            }
        }
    }

    std::println("END");
    return 0;
}
