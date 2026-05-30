// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for atdecc::aem::DescriptorStorage::create() and the
/// public iteration methods that follow on success. Targets the bounds
/// checks in DescriptorStorage that prior review identified as
/// integer-overflow-vulnerable (now widened to size_t with checked
/// multiplication; see core/statusbar/safe_arith/). The decoder must not
/// crash, hang, or read out of bounds on any input.

#include "statusbar/atdecc/atdecc_descriptor_storage.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const blob = std::span<uint8_t const>(data, size);

    auto storage_result = statusbar::atdecc::aem::DescriptorStorage::create(blob);
    if (!storage_result) {
        return 0;
    }
    auto const& storage = *storage_result;

    // Exercise iteration / lookup paths that walk the TOC and symbol table.
    auto const cfg_count = storage.get_configuration_count();
    for (uint16_t cfg = 0; cfg < cfg_count && cfg < 16; ++cfg) {
        for (uint16_t type = 0; type < 4; ++type) {
            for (uint16_t index = 0; index < 4; ++index) {
                (void)storage.get_descriptor(cfg, type, index);
                (void)storage.get_symbol(cfg, type, index);
            }
        }
    }
    return 0;
}
