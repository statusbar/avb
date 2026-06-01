// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for AdpDu deserialization via length-checked load().

#include "statusbar/atdecc/atdecc_adp.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const packet = std::span<uint8_t const>(data, size);
    statusbar::atdecc::AdpDu du{};
    if (statusbar::protocol::load(packet, &du)) {
        (void)du.is_valid();
        (void)du.message_type();
        (void)du.control_data_length();
    }
    return 0;
}
