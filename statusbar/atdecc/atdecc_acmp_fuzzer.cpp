// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for AcmpDu / AcmpDu2021 deserialization via length-checked
/// load(). Both wire layouts are exercised on the same input.

#include "statusbar/atdecc/atdecc_acmp_pdu.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const packet = std::span<uint8_t const>(data, size);

    statusbar::atdecc::AcmpDu du{};
    if (statusbar::protocol::load(packet, &du)) {
        (void)du.message_type();
        (void)du.status();
    }

    statusbar::atdecc::AcmpDu2021 du2021{};
    if (statusbar::protocol::load(packet, &du2021)) {
        auto const sink = du2021;
        (void)sink;
    }
    return 0;
}
