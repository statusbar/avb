#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/owlm/owlm_eui64.hpp"
#include "statusbar/owlm/owlm_packet.hpp"
#include "statusbar/udptun/udptun_codec_concept.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace statusbar::owlm {

/// Codec adapter for the OWLM measurement packet. Stateless — the
/// 32-byte header carries everything the framework needs.
///
/// Primary vs redundant is encoded in the EUI-64 mid bytes
/// (`PRIMARY_MID_BYTES` / `REDUNDANT_MID_BYTES`); `eui64_pair_id`
/// canonicalizes both to the same logical sender so the per-source
/// tracker shows one row per pair instead of two.
class OwlmCodec
{
  public:
    using DecodedPacket = OwlmPacket;

    [[nodiscard]] static constexpr auto header_size() noexcept -> size_t { return OwlmPacket::HEADER_SIZE; }

    auto encode(std::span<uint8_t> buf, ieee::Eui64 sender_id, uint32_t sequence, int64_t tx_gptp_ns, uint32_t interval_us)
        const noexcept -> size_t
    {
        OwlmPacket p{};
        p.sender_eui64 = sender_id;
        p.sequence = sequence;
        p.tx_gptp_ns = tx_gptp_ns;
        p.tx_interval_us = interval_us;
        return encode_owlm_packet(p, buf);
    }

    [[nodiscard]] auto decode(std::span<uint8_t const> bytes) const noexcept -> std::optional<DecodedPacket>
    {
        OwlmPacket p{};
        auto const ec = decode_owlm_packet(bytes, p);
        if (ec) {
            return std::nullopt;
        }
        return p;
    }

    [[nodiscard]] auto validate_for_reflect(std::span<uint8_t const> bytes) const noexcept -> bool
    {
        return decode(bytes).has_value();
    }

    [[nodiscard]] auto classify(DecodedPacket const& p, ieee::Eui64 const& my_pair_id) const noexcept -> udptun::PacketRole
    {
        auto const pair = eui64_pair_id(p.sender_eui64);
        bool const is_self = (pair == my_pair_id);
        auto const mid = eui64_mid_bytes(p.sender_eui64);
        if (mid == PRIMARY_MID_BYTES) {
            return is_self ? udptun::PacketRole::SelfPrimary : udptun::PacketRole::RemotePrimary;
        }
        if (mid == REDUNDANT_MID_BYTES) {
            return is_self ? udptun::PacketRole::SelfRedundant : udptun::PacketRole::RemoteRedundant;
        }
        return is_self ? udptun::PacketRole::SelfLegacy : udptun::PacketRole::RemoteLegacy;
    }

    [[nodiscard]] auto sender_id(DecodedPacket const& p) const noexcept -> ieee::Eui64 { return p.sender_eui64; }
    [[nodiscard]] auto sender_pair_id(DecodedPacket const& p) const noexcept -> ieee::Eui64
    {
        return eui64_pair_id(p.sender_eui64);
    }
    [[nodiscard]] auto sequence(DecodedPacket const& p) const noexcept -> uint32_t { return p.sequence; }
    /// Returns the packet's presentation time
    /// (`acquisition_wall + worst_case_latency`). Field name kept for
    /// backwards compatibility — see udptun::Codec doc.
    [[nodiscard]] auto tx_gptp_ns(DecodedPacket const& p) const noexcept -> int64_t { return p.tx_gptp_ns; }
    [[nodiscard]] auto announced_interval_us(DecodedPacket const& p) const noexcept -> uint32_t { return p.tx_interval_us; }
};

static_assert(udptun::Codec<OwlmCodec>, "OwlmCodec must satisfy the udptun::Codec concept");

}  // namespace statusbar::owlm
