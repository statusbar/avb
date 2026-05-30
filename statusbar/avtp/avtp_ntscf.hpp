#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// NTSCF - Non-Time-Synchronous Control Format - IEEE 1722-2025 Clause 9.2
/// Alternative header for non-time-synchronous ACF control messages.
///
/// Wire format version 0 (12-byte header + variable acf_payload_data):
///   Byte 0:      subtype (0x82)
///   Byte 1:      sv[7] | version[6:4] | r[3] | ntscf_data_length[10:8]
///   Byte 2:      ntscf_data_length[7:0]
///   Byte 3:      sequence_num_lsb[7:0]
///   Bytes 4-11:  stream_id (8 bytes)
///   Bytes 12+:   acf_payload_data

#include "statusbar/avtp/avtp_types.hpp"
#include "statusbar/ieee/ieee_base.hpp"
#include "statusbar/tsn/tsn_stream_id.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace statusbar::avtp {

using ieee::doublet_t;
using ieee::octet_t;
using tsn::StreamId;

//
// NtscfPdu - IEEE 1722-2025 Clause 9.2, Figure 58 (version 0)
// Based on AVTPDU alternative header version 0 (Figure 13)
//

/// NTSCF Protocol Data Unit Header - IEEE 1722-2025 Clause 9.2 (subtype 0x82)
struct NtscfPdu
{
    /// Total length of NTSCF header on wire (version 0)
    static constexpr size_t LENGTH = 12;
    static constexpr size_t HEADER_LENGTH = LENGTH;

    // Byte 0: subtype[7:0] = 0x82 for NTSCF
    octet_t subtype;

    // Byte 1: sv[7] | version[6:4] | r[3] | ntscf_data_length[10:8]
    octet_t sv_version_r_len_hi;

    // Byte 2: ntscf_data_length[7:0]
    octet_t ntscf_data_length_lo;

    // Byte 3: sequence_num_lsb[7:0]
    octet_t sequence_num_lsb;

    // Bytes 4-11: stream_id
    StreamId stream_id_;

    // Accessors - Control flags (byte 1)

    /// Get the stream valid (sv) bit
    [[nodiscard]] constexpr auto sv() const noexcept -> bool { return sv_version_r_len_hi.has_flag(0x80U); }

    /// Set the stream valid (sv) bit
    constexpr void set_sv(bool const value) noexcept { sv_version_r_len_hi.set_flag(0x80U, value); }

    /// Get the version field (bits 6:4)
    [[nodiscard]] constexpr auto version() const noexcept -> uint8_t { return sv_version_r_len_hi.get_bits(0x70U, 4); }

    // Accessors - Data length (11-bit field spanning bytes 1-2)

    /// Get the ntscf_data_length (11-bit field, acf_payload_data length in octets)
    [[nodiscard]] constexpr auto ntscf_data_length() const noexcept -> uint16_t
    {
        uint16_t const hi = static_cast<uint16_t>(sv_version_r_len_hi.get() & 0x07U) << 8;
        uint16_t const lo = ntscf_data_length_lo.get();
        return static_cast<uint16_t>(hi | lo);
    }

    /// Set the ntscf_data_length (11-bit field, max 2047)
    constexpr void set_ntscf_data_length(uint16_t const length) noexcept
    {
        uint8_t const current = sv_version_r_len_hi.get();
        sv_version_r_len_hi = static_cast<uint8_t>((current & 0xF8U) | ((length >> 8) & 0x07U));
        ntscf_data_length_lo = static_cast<uint8_t>(length & 0xFFU);
    }

    // Accessors - Sequence number

    /// Get the sequence_num_lsb (8-bit sequence counter)
    [[nodiscard]] constexpr auto get_sequence_num_lsb() const noexcept -> uint8_t { return sequence_num_lsb.get(); }

    /// Set the sequence_num_lsb
    constexpr void set_sequence_num_lsb(uint8_t const seq) noexcept { sequence_num_lsb = seq; }

    // Accessors - Stream ID

    /// Get the stream ID
    [[nodiscard]] constexpr auto stream_id() const noexcept -> StreamId { return stream_id_; }

    /// Set the stream ID
    constexpr void set_stream_id(StreamId const& sid) noexcept { stream_id_ = sid; }

    // Initialization

    /// Initialize for NTSCF
    /// @param sid Stream ID
    /// @param sv_valid Whether stream_id has a valid reservation (sv bit)
    constexpr void init(StreamId const& sid, bool const sv_valid = true) noexcept
    {
        subtype = AvtpSubtype::ntscf;
        sv_version_r_len_hi = 0U;
        set_sv(sv_valid);
        ntscf_data_length_lo = 0U;
        sequence_num_lsb = 0U;
        stream_id_ = sid;
    }

    // Validation

    /// Check if this is a valid NTSCF header
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool
    {
        if (subtype != AvtpSubtype::ntscf) {
            return false;
        }
        if (version() != 0) {
            return false;
        }
        return true;
    }

    auto operator<=>(NtscfPdu const& rhs) const noexcept -> std::strong_ordering = default;
};

// Compile-time layout verification
static_assert(sizeof(NtscfPdu) == 12, "NtscfPdu must be exactly 12 bytes");
static_assert(alignof(NtscfPdu) <= 4, "NtscfPdu alignment must not exceed 4 bytes");
static_assert(offsetof(NtscfPdu, subtype) == 0, "subtype must be at offset 0");
static_assert(offsetof(NtscfPdu, sv_version_r_len_hi) == 1, "sv_version_r_len_hi must be at offset 1");
static_assert(offsetof(NtscfPdu, ntscf_data_length_lo) == 2, "ntscf_data_length_lo must be at offset 2");
static_assert(offsetof(NtscfPdu, sequence_num_lsb) == 3, "sequence_num_lsb must be at offset 3");
static_assert(offsetof(NtscfPdu, stream_id_) == 4, "stream_id must be at offset 4");

}  // namespace statusbar::avtp

// Serialization trait
template <>
struct statusbar::traits::is_serializable_wire_fixed_struct<statusbar::avtp::NtscfPdu> : std::true_type
{};

namespace statusbar::avtp {

using protocol::load_unchecked;
using protocol::store_unchecked;

//
// Parse/create helpers
//

/// Parse an NTSCF header from a packet
/// @param packet Raw packet data (Ethernet payload starting at subtype byte)
[[nodiscard]] auto ntscf_parse_header(std::span<uint8_t const> packet) noexcept -> std::optional<NtscfPdu>;

/// Get ACF payload data span from an NTSCF packet
/// @param packet Raw packet data including the NTSCF header
[[nodiscard]] auto ntscf_get_acf_payload(std::span<uint8_t const> packet) noexcept -> std::span<uint8_t const>;

}  // namespace statusbar::avtp
