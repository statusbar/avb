// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for MaapDu deserialization. Uses the length-checked
/// statusbar::protocol::load() (NOT load_unchecked, which asserts on short buffers) so
/// arbitrary input lengths are safe. Exercises is_valid() / accessors on
/// success. Looks for OOB/UB via ASan/UBSan.

#include "statusbar/avtp/avtp_maap.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const packet = std::span<uint8_t const>(data, size);
    statusbar::avtp::MaapDu du{};
    if (statusbar::protocol::load(packet, &du)) {
        (void)du.is_valid();
        (void)du.message_type();
        (void)du.maap_data_length();
    }
    return 0;
}
