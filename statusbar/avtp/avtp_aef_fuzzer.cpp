// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for avtp::aef_continuous_parse_header and
/// avtp::aef_discrete_parse_header. Both consume raw AVTP/AEF wire bytes from an
/// untrusted source; neither may crash or read out of bounds on any input.

#include "statusbar/avtp/avtp_aef.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const packet = std::span<uint8_t const>(data, size);

    auto const cont = statusbar::avtp::aef_continuous_parse_header(packet);
    if (cont.has_value()) {
        auto const sink = *cont;
        (void)sink;
    }

    auto const disc = statusbar::avtp::aef_discrete_parse_header(packet);
    if (disc.has_value()) {
        auto const sink = *disc;
        (void)sink;
    }
    return 0;
}
