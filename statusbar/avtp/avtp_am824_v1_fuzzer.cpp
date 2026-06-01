// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for avtp::am824_v1_parse_header. Consumes raw AVTP wire bytes from an
/// untrusted source; must not crash or read out of bounds on any input. May
/// legally return std::nullopt. We look for OOB/UB via ASan/UBSan.

#include "statusbar/avtp/avtp_am824_v1.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const packet = std::span<uint8_t const>(data, size);
    auto const pdu = statusbar::avtp::am824_v1_parse_header(packet);
    if (pdu.has_value()) {
        auto const sink = *pdu;
        (void)sink;
    }
    return 0;
}
