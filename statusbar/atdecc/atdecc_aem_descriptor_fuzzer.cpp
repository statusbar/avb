// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for aem::parse_descriptor. Consumes raw descriptor bytes
/// from an untrusted source; must not crash or read out of bounds on any input.

#include "statusbar/atdecc/atdecc_aem_format.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const desc = std::span<uint8_t const>(data, size);
    auto const parsed = statusbar::atdecc::aem::parse_descriptor(desc);
    (void)parsed;
    return 0;
}
