#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// IP Encapsulation of AVTP - IEEE 1722-2025 Annex J
/// UDP encapsulation of any AVTPDU with a 4-byte sequence number prefix.
///
/// Wire format (IP AVTPDU datagram payload after UDP header):
///   Bytes 0-3:  encapsulation_sequence_num (32-bit, network byte order)
///   Bytes 4+:   encapsulated_avtpdu (standard AVTPDU as defined in 4.7)
///
/// Destination ports (J.5):
///   17220 - continuous encapsulation (latency-sensitive streams)
///   17221 - discrete encapsulation (control/discovery messages)
///
/// Source port (J.4): ephemeral, never 17220 or 17221.

#include "statusbar/avtp/avtp_types.hpp"
#include "statusbar/ieee/ieee_base.hpp"
#include "statusbar/ip/ip_port_numbers.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace statusbar::avtp {

using ieee::quadlet_t;

//
// Constants
//

/// Size of the IP AVTPDU encapsulation header (4 bytes)
constexpr size_t IP_AVTPDU_HEADER_LENGTH = 4;

/// Default destination port for continuous (latency-sensitive) AVTP streams
constexpr uint16_t IP_AVTPDU_PORT_CONTINUOUS = ip::port::AVTP;  // 17220

/// Default destination port for discrete (control/discovery) AVTP messages
constexpr uint16_t IP_AVTPDU_PORT_DISCRETE = ip::port::ATDECC;  // 17221

//
// IpAvtpduHeader - IEEE 1722-2025 Annex J, Figure J.1
//

/// IP AVTPDU encapsulation header (4-byte prefix before the AVTPDU)
///
/// The encapsulation_sequence_num is a 32-bit counter that increments by one
/// for each IP AVTPDU sent from a source port to a specific destination IP
/// address and port. Listeners use it to detect lost, duplicate, or
/// out-of-order packets.
struct IpAvtpduHeader
{
    static constexpr size_t LENGTH = 4;

    // Bytes 0-3: encapsulation_sequence_num (network byte order)
    quadlet_t encapsulation_sequence_num_;

    // Accessors

    /// Get the encapsulation sequence number
    [[nodiscard]] constexpr auto encapsulation_sequence_num() const noexcept -> uint32_t
    {
        return encapsulation_sequence_num_.get();
    }

    /// Set the encapsulation sequence number
    /// @param seq_num The sequence number value
    constexpr void set_encapsulation_sequence_num(uint32_t const seq_num) noexcept { encapsulation_sequence_num_ = seq_num; }

    // Initialization

    /// Initialize with a starting sequence number
    /// @param initial_seq Starting sequence number (may be any value per J.3.2)
    constexpr void init(uint32_t const initial_seq = 0) noexcept { encapsulation_sequence_num_ = initial_seq; }

    auto operator<=>(IpAvtpduHeader const& rhs) const noexcept -> std::strong_ordering = default;
};

// Compile-time layout verification
static_assert(sizeof(IpAvtpduHeader) == 4, "IpAvtpduHeader must be exactly 4 bytes");
static_assert(alignof(IpAvtpduHeader) <= 4, "IpAvtpduHeader alignment must not exceed 4 bytes");
static_assert(offsetof(IpAvtpduHeader, encapsulation_sequence_num_) == 0, "encapsulation_sequence_num must be at offset 0");

}  // namespace statusbar::avtp

// Serialization trait — must be before any use of load_unchecked/store_unchecked with IpAvtpduHeader
template <>
struct statusbar::traits::is_serializable_wire_fixed_struct<statusbar::avtp::IpAvtpduHeader> : std::true_type
{};

namespace statusbar::avtp {

using protocol::load_unchecked;
using protocol::store_unchecked;

//
// Port selection
//

/// Get the destination UDP port for an IP AVTPDU based on the encapsulated subtype.
/// Per IEEE 1722-2025 J.5:
///   - Continuous subtypes -> 17220
///   - Discrete subtypes  -> 17221
/// @param subtype The AVTP subtype of the encapsulated AVTPDU
/// @return The default destination port (17220 or 17221)
[[nodiscard]] constexpr auto ip_avtpdu_destination_port(uint8_t const subtype) noexcept -> uint16_t
{
    return avtp_is_continuous(subtype) ? IP_AVTPDU_PORT_CONTINUOUS : IP_AVTPDU_PORT_DISCRETE;
}

//
// Parse helpers
//

/// Parse an IP AVTPDU header from a UDP payload.
/// @param udp_payload The UDP datagram payload (must be >= 4 bytes)
/// @return The parsed header, or nullopt if the payload is too small
[[nodiscard]] inline auto ip_avtpdu_parse_header(std::span<uint8_t const> const udp_payload) noexcept
    -> std::optional<IpAvtpduHeader>
{
    if (udp_payload.size() < IP_AVTPDU_HEADER_LENGTH) {
        return std::nullopt;
    }
    IpAvtpduHeader header{};
    (void)load_unchecked(udp_payload, &header);
    return header;
}

/// Get the encapsulated AVTPDU from a UDP payload (bytes after the 4-byte header).
/// @param udp_payload The UDP datagram payload
/// @return Span of the encapsulated AVTPDU, or empty span if payload is too small
[[nodiscard]] inline auto ip_avtpdu_get_payload(std::span<uint8_t const> const udp_payload) noexcept -> std::span<uint8_t const>
{
    if (udp_payload.size() <= IP_AVTPDU_HEADER_LENGTH) {
        return {};
    }
    return udp_payload.subspan(IP_AVTPDU_HEADER_LENGTH);
}

//
// IpAvtpduSequencer - Sequence number generator for talker-side use
//

/// Stateful sequence number generator for IP AVTPDU transmission.
/// Tracks a 32-bit counter per stream, incrementing by one on each call.
/// The counter wraps from 0xFFFFFFFF to 0x00000000 as specified in J.3.2.
class IpAvtpduSequencer
{
  public:
    /// Construct with an initial sequence number (may be any value per J.3.2)
    /// @param initial Starting sequence number
    constexpr explicit IpAvtpduSequencer(uint32_t const initial = 0) noexcept
        : seq_{initial}
    {}

    /// Get the next sequence number and advance the counter.
    /// @return The current sequence number before incrementing
    [[nodiscard]] constexpr auto next() noexcept -> uint32_t
    {
        uint32_t const current = seq_;
        ++seq_;  // wraps naturally at 2^32
        return current;
    }

    /// Get the current sequence number without advancing.
    [[nodiscard]] constexpr auto current() const noexcept -> uint32_t { return seq_; }

    /// Reset to a new starting value.
    /// @param value New sequence number
    constexpr void reset(uint32_t const value = 0) noexcept { seq_ = value; }

  private:
    uint32_t seq_;
};

}  // namespace statusbar::avtp
