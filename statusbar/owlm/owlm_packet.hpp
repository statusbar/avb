#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/ieee/ieee.hpp"
#include "statusbar/owlm/owlm_error.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>

namespace statusbar::owlm {

/// Wire-format header for an OWLM measurement packet. All multi-byte
/// fields encode/decode as network byte order (big-endian). Total
/// fixed header = 32 bytes; an optional zero-fill payload may follow
/// on the wire.
///
/// `tx_gptp_ns` carries the **presentation time** (PT) the sender
/// stamped on the packet: `PT = acquisition_wall + worst_case_latency`.
/// The receiver uses PT as the slot key in the redundancy / reordering
/// tracker (so reordering within RTT is implicit) and computes
/// `lateness = rx_wall - PT` as its primary diagnostic. With
/// worst_case_latency = 0 PT collapses to the acquisition wall and
/// lateness equals real one-way transit time. The on-wire field name
/// is retained for backwards compatibility; the semantic meaning has
/// shifted.
struct OwlmPacket
{
    static constexpr size_t HEADER_SIZE = 32;
    static constexpr uint32_t MAGIC = 0x4F574C4DU;  // "OWLM"
    static constexpr uint16_t VERSION = 1;
    static constexpr uint32_t max_interval_us = 60'000'000U;  // 60 s

    ieee::Eui64 sender_eui64{};
    uint32_t sequence{0};
    int64_t tx_gptp_ns{0};  ///< presentation time on the wire timeline
    uint32_t tx_interval_us{0};
};

/// Encode an OwlmPacket into the first 32 bytes of `buf`. Returns the
/// number of bytes written (always `HEADER_SIZE`). Asserts buf size.
auto encode_owlm_packet(OwlmPacket const& p, std::span<uint8_t> buf) -> size_t;

/// Decode and validate. Returns a non-empty error_code on any
/// validation failure (per spec section 5.2). On success the returned
/// error_code converts to false in boolean context.
[[nodiscard]] auto decode_owlm_packet(std::span<uint8_t const> buf, OwlmPacket& out) -> std::error_code;

}  // namespace statusbar::owlm
