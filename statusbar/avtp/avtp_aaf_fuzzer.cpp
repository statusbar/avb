// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for avtp::aaf_parse_header. The parser consumes raw
/// AVTP/AAF wire bytes from an untrusted source and must not crash or
/// read out of bounds on any input. May legally return std::nullopt;
/// we look for UB via ASan/UBSan.

#include "statusbar/avtp/avtp_aaf.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const packet = std::span<uint8_t const>(data, size);
    auto const pdu = statusbar::avtp::aaf_parse_header(packet);
    if (pdu.has_value()) {
        // Touch fields so any deferred lazy evaluation is exercised.
        (void)pdu->stream_data_length;
        (void)pdu->sv_version_flags;
    }
    return 0;
}
