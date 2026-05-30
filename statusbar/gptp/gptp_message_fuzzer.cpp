// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for gptp::parse_gptp. Consumes IEEE 802.1AS PTP
/// payload bytes from an untrusted source. The parser dispatches by
/// message type into per-message decoders (Sync, Announce, Follow_Up,
/// Pdelay_*) — each of which has its own bounds-sensitive layout.

#include "statusbar/gptp/gptp_messages.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const payload = std::span<uint8_t const>(data, size);
    (void)statusbar::gptp::parse_gptp(payload);
    return 0;
}
