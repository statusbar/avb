#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/ieee/ieee.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace statusbar::udptun {

/// Outcome of `Codec::classify(packet, my_pair_id)`. The framework uses
/// this to route received packets between RTT (self-loopback), the
/// per-source one-way tracker (remote), and the redundancy tracker.
enum class PacketRole
{
    /// Our own primary packet looped back from a reflector or peer that
    /// happens to share our pair_id. Feed RTT and self_redundancy.on_primary().
    SelfPrimary,
    /// Our own redundant packet looped back. Feed self_redundancy.on_redundant();
    /// only feed RTT if the redundant rescued a primary loss (caller checks).
    SelfRedundant,
    /// Our own packet from a single-stream / legacy mode (no primary/redundant
    /// distinction at the codec level). Feed RTT only.
    SelfLegacy,
    /// A remote primary. Feed PerSourceTracker.observe().
    RemotePrimary,
    /// A remote redundant. Drop silently to avoid double-counting in the
    /// per-source list (the receiver tracks the logical stream by pair_id;
    /// counting both primary and redundant would inflate received_count).
    RemoteRedundant,
    /// A remote single-stream / legacy packet. Feed PerSourceTracker.observe().
    RemoteLegacy,
};

/// Concept requirements for a udptun packet codec.
///
/// A codec is the integration point between the framework's generic
/// TX/RX scaffolding and a specific wire format. It carries whatever
/// per-stream configuration the format needs (sample rate, channel
/// count, etc.) and exposes the framework's view of a packet via a
/// fixed set of accessors.
///
/// Stateless codecs (e.g., OwlmCodec) can be default-constructed.
/// Stateful codecs (e.g., AAF v1 over Annex J) take their config in
/// the constructor.
///
/// Two reference codecs are anticipated:
///
///   - `owlm::OwlmCodec` — 32-byte OWLM measurement header. Stateless.
///     Primary/redundant distinguished by EUI-64 mid bytes.
///
///   - `udptun::AafV1OverAnnexJCodec` — IEEE 1722-2025 AAF version 1
///     header carried over IEEE 1722 Annex J UDP encapsulation, with the
///     64-bit avtp_timestamp in TAI (see udptun_aaf_v1_codec.hpp).
///     Stateful (carries format/channels/bit_depth/sample_rate).
///     Primary/redundant distinguished by configured stream_id pair.
template <typename C>
concept Codec = requires(
    C const& codec,
    std::span<uint8_t> wbuf,
    std::span<uint8_t const> rbuf,
    ieee::Eui64 sender_id,
    typename C::DecodedPacket const& pkt,
    ieee::Eui64 my_pair_id,
    uint32_t seq,
    int64_t tx_gptp_ns,
    uint32_t interval_us) {
    /// Codec-specific decoded packet representation. Treated as opaque
    /// by the framework — only the accessors below are called on it.
    typename C::DecodedPacket;

    /// Wire size of the codec's header (excluding any payload). The
    /// framework uses this to pre-size the TX scratch buffer.
    { codec.header_size() } -> std::convertible_to<size_t>;

    /// Encode a packet header into wbuf starting at offset 0. Returns
    /// the number of bytes written. The framework guarantees
    /// `wbuf.size() >= header_size() + payload_bytes`; bytes past the
    /// header are left untouched (the framework zeros them on initial
    /// allocation and reuses them across redundant emissions).
    { codec.encode(wbuf, sender_id, seq, tx_gptp_ns, interval_us) } -> std::convertible_to<size_t>;

    /// Decode a received datagram into the codec's parsed
    /// representation. Returns nullopt on any parse / validation error.
    { codec.decode(rbuf) } -> std::convertible_to<std::optional<typename C::DecodedPacket>>;

    /// Reflector-side validation. Should return true iff this is a
    /// well-formed packet of this codec's wire format. Typical
    /// implementation: `return decode(bytes).has_value();`.
    { codec.validate_for_reflect(rbuf) } -> std::convertible_to<bool>;

    /// Classify a decoded packet's role relative to the local identity.
    /// The framework uses the result to route the packet (RTT vs
    /// per-source tracker vs redundancy tracker; primary vs redundant).
    { codec.classify(pkt, my_pair_id) } -> std::convertible_to<PacketRole>;

    /// The packet's literal sender identifier as carried on the wire.
    { codec.sender_id(pkt) } -> std::convertible_to<ieee::Eui64>;

    /// The canonical "logical sender" identifier — for codecs that pair
    /// primary and redundant under one logical stream, this collapses
    /// both to the same value so PerSourceTracker shows one row per
    /// pair. Codecs without primary/redundant pairing can return
    /// `sender_id(pkt)` here.
    { codec.sender_pair_id(pkt) } -> std::convertible_to<ieee::Eui64>;

    /// The packet's sequence number used for loss / redundancy tracking.
    /// 32-bit; codecs with smaller wire fields are responsible for
    /// reconstruction (e.g., AAF v0 8-bit seq → unwrapped 32-bit) at
    /// decode time.
    { codec.sequence(pkt) } -> std::convertible_to<uint32_t>;

    /// The packet's **presentation time** on the gPTP-rated wire
    /// timeline (or CLOCK_REALTIME under --no-gptp), in nanoseconds:
    /// `PT = acquisition_wall + worst_case_latency`. The framework
    /// uses this as the slot key for the redundancy / reordering
    /// tracker and as the basis for the receiver's lateness metric
    /// (`rx_wall - PT`). The accessor name is kept for backwards
    /// compatibility; with worst_case_latency == 0 PT equals the
    /// acquisition wall (the original semantic).
    { codec.tx_gptp_ns(pkt) } -> std::convertible_to<int64_t>;

    /// The TX interval the sender announces. Used by PerSourceTracker
    /// for loss accounting. Codecs that don't carry this on the wire
    /// can return their own configured interval.
    { codec.announced_interval_us(pkt) } -> std::convertible_to<uint32_t>;
};

}  // namespace statusbar::udptun
