#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// NTSCF Version 1 - Non-Time-Synchronous Control Format with version 1 alternative header
/// IEEE 1722-2025 Clause 9.2 with version 1 header per 4.7.6
///
/// Version 1 improvements over version 0:
///   - sequence_num: 32-bit (was 8-bit via sequence_num_lsb only)
///   - ptp_grandmaster_identity: 8 bytes - clock domain identification
///
/// Wire format version 1 (28-byte header + variable acf_payload_data):
///   Byte 0:      subtype (0x82)
///   Byte 1:      sv[7] | version=1[6:4] | reserved[3:0]
///   Bytes 2-3:   reserved (continuation of 20-bit reserved field)
///   Bytes 4-7:   sequence_num (32-bit)
///   Bytes 8-15:  ptp_grandmaster_identity (8 bytes)
///   Byte 16:     reserved[7:4] | r[3] | ntscf_data_length[10:8]
///   Byte 17:     ntscf_data_length[7:0]
///   Byte 18:     sequence_num_lsb (low 8 bits copy of sequence_num)
///   Byte 19:     reserved
///   Bytes 20-27: stream_id (8 bytes)
///   Bytes 28+:   acf_payload_data

#include "statusbar/avtp/avtp_types.hpp"
#include "statusbar/ieee/ieee_base.hpp"
#include "statusbar/tsn/tsn_clock_identity.hpp"
#include "statusbar/tsn/tsn_stream_id.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace statusbar::avtp {

using ieee::doublet_t;
using ieee::octet_t;
using ieee::quadlet_t;
using tsn::ClockIdentity;
using tsn::StreamId;

//
// NtscfV1Pdu - IEEE 1722-2025 Clause 9.2 (version 1)
// Based on AVTPDU alternative header version 1 (Figure 14)
//

/// NTSCF Protocol Data Unit Header version 1 - IEEE 1722-2025 Clause 9.2 (subtype 0x82, version 1)
struct NtscfV1Pdu
{
    /// Total length of NTSCF V1 header on wire
    static constexpr size_t LENGTH = 28;
    static constexpr size_t HEADER_LENGTH = LENGTH;

    // ---- Alternative header common fields (bytes 0-15) ----

    /// Byte 0: subtype[7:0] = 0x82 for NTSCF
    octet_t subtype;

    /// Byte 1: sv[7] | version[6:4] | reserved[3:0]
    octet_t sv_version_rsv;

    /// Bytes 2-3: reserved (continuation of 20-bit reserved field)
    doublet_t reserved1;

    /// Bytes 4-7: sequence_num (32-bit)
    quadlet_t sequence_num;

    /// Bytes 8-15: ptp_grandmaster_identity
    ClockIdentity ptp_grandmaster_identity;

    // ---- NTSCF format-specific fields (alternative_data_payload, bytes 16+) ----

    /// Byte 16: reserved[7:4] | r[3] | ntscf_data_length[10:8]
    octet_t rsv_r_len_hi;

    /// Byte 17: ntscf_data_length[7:0]
    octet_t ntscf_data_length_lo;

    /// Byte 18: sequence_num_lsb (low 8 bits copy of sequence_num)
    octet_t sequence_num_lsb;

    /// Byte 19: reserved
    octet_t reserved2;

    /// Bytes 20-27: stream_id
    StreamId stream_id_;

    // ========== Accessors - Common alternative header fields ==========

    /// Get the stream valid (sv) bit
    [[nodiscard]] constexpr auto sv() const noexcept -> bool { return sv_version_rsv.has_flag(0x80U); }

    /// Set the stream valid (sv) bit
    constexpr void set_sv(bool const value) noexcept { sv_version_rsv.set_flag(0x80U, value); }

    /// Get the version field (bits 6:4) — should be 1 for this struct
    [[nodiscard]] constexpr auto version() const noexcept -> uint8_t { return sv_version_rsv.get_bits(0x70U, 4); }

    // ========== Accessors - Version 1 specific fields ==========

    /// Get the sequence number (32-bit)
    [[nodiscard]] constexpr auto get_sequence_num() const noexcept -> uint32_t { return sequence_num.get(); }

    /// Set the sequence number (also updates sequence_num_lsb)
    constexpr void set_sequence_num(uint32_t const value) noexcept
    {
        sequence_num = value;
        sequence_num_lsb = static_cast<uint8_t>(value & 0xFFU);
    }

    /// Increment sequence number (wraps at 2^32, updates lsb)
    constexpr void increment_sequence_num() noexcept { set_sequence_num(get_sequence_num() + 1U); }

    /// Get the PTP grandmaster identity
    [[nodiscard]] constexpr auto get_ptp_grandmaster_identity() const noexcept -> ClockIdentity { return ptp_grandmaster_identity; }

    /// Set the PTP grandmaster identity
    constexpr void set_ptp_grandmaster_identity(ClockIdentity const& gm) noexcept { ptp_grandmaster_identity = gm; }

    // ========== Accessors - NTSCF format-specific fields ==========

    /// Get the ntscf_data_length (11-bit field, acf_payload_data length in octets)
    [[nodiscard]] constexpr auto ntscf_data_length() const noexcept -> uint16_t
    {
        uint16_t const hi = static_cast<uint16_t>(rsv_r_len_hi.get() & 0x07U) << 8;
        uint16_t const lo = ntscf_data_length_lo.get();
        return static_cast<uint16_t>(hi | lo);
    }

    /// Set the ntscf_data_length (11-bit field, max 2047)
    constexpr void set_ntscf_data_length(uint16_t const length) noexcept
    {
        uint8_t const current = rsv_r_len_hi.get();
        rsv_r_len_hi = static_cast<uint8_t>((current & 0xF8U) | ((length >> 8) & 0x07U));
        ntscf_data_length_lo = static_cast<uint8_t>(length & 0xFFU);
    }

    /// Get the sequence_num_lsb (low 8 bits copy of sequence_num)
    [[nodiscard]] constexpr auto get_sequence_num_lsb() const noexcept -> uint8_t { return sequence_num_lsb.get(); }

    /// Set the sequence_num_lsb
    constexpr void set_sequence_num_lsb(uint8_t const seq) noexcept { sequence_num_lsb = seq; }

    /// Get the stream ID
    [[nodiscard]] constexpr auto stream_id() const noexcept -> StreamId { return stream_id_; }

    /// Set the stream ID
    constexpr void set_stream_id(StreamId const& sid) noexcept { stream_id_ = sid; }

    // ========== Initialization ==========

    /// Initialize for NTSCF version 1
    /// @param sid Stream ID
    /// @param sv_valid Whether stream_id has a valid reservation (sv bit)
    constexpr void init(StreamId const& sid, bool const sv_valid = true) noexcept
    {
        subtype = AvtpSubtype::ntscf;
        sv_version_rsv = 0x00U;
        set_sv(sv_valid);
        sv_version_rsv = static_cast<uint8_t>(sv_version_rsv.get() | 0x10U);  // version=1
        reserved1 = 0U;
        sequence_num = 0U;
        ptp_grandmaster_identity = ClockIdentity{};
        rsv_r_len_hi = 0U;
        ntscf_data_length_lo = 0U;
        sequence_num_lsb = 0U;
        reserved2 = 0U;
        stream_id_ = sid;
    }

    // ========== Validation ==========

    /// Check if this is a valid NTSCF version 1 header
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool
    {
        if (subtype != AvtpSubtype::ntscf) {
            return false;
        }
        if (version() != 1) {
            return false;
        }
        return true;
    }

    auto operator<=>(NtscfV1Pdu const& rhs) const noexcept -> std::strong_ordering = default;
};

// Compile-time layout verification
static_assert(sizeof(NtscfV1Pdu) == 28, "NtscfV1Pdu must be exactly 28 bytes");
static_assert(alignof(NtscfV1Pdu) <= 4, "NtscfV1Pdu alignment must not exceed 4 bytes");
static_assert(offsetof(NtscfV1Pdu, subtype) == 0, "subtype must be at offset 0");
static_assert(offsetof(NtscfV1Pdu, sv_version_rsv) == 1, "sv_version_rsv must be at offset 1");
static_assert(offsetof(NtscfV1Pdu, reserved1) == 2, "reserved1 must be at offset 2");
static_assert(offsetof(NtscfV1Pdu, sequence_num) == 4, "sequence_num must be at offset 4");
static_assert(offsetof(NtscfV1Pdu, ptp_grandmaster_identity) == 8, "ptp_grandmaster_identity must be at offset 8");
static_assert(offsetof(NtscfV1Pdu, rsv_r_len_hi) == 16, "rsv_r_len_hi must be at offset 16");
static_assert(offsetof(NtscfV1Pdu, ntscf_data_length_lo) == 17, "ntscf_data_length_lo must be at offset 17");
static_assert(offsetof(NtscfV1Pdu, sequence_num_lsb) == 18, "sequence_num_lsb must be at offset 18");
static_assert(offsetof(NtscfV1Pdu, reserved2) == 19, "reserved2 must be at offset 19");
static_assert(offsetof(NtscfV1Pdu, stream_id_) == 20, "stream_id must be at offset 20");

}  // namespace statusbar::avtp

// Serialization trait
template <>
struct statusbar::traits::is_serializable_wire_fixed_struct<statusbar::avtp::NtscfV1Pdu> : std::true_type
{};

namespace statusbar::avtp {

using protocol::load_unchecked;
using protocol::store_unchecked;

//
// Parse/create helpers
//

/// Parse an NTSCF V1 header from a packet
/// @param packet Raw packet data (must have subtype==NTSCF and version==1)
[[nodiscard]] auto ntscf_v1_parse_header(std::span<uint8_t const> packet) noexcept -> std::optional<NtscfV1Pdu>;

/// Get ACF payload data span from an NTSCF V1 packet
/// @param packet Raw packet data including the NTSCF V1 header
[[nodiscard]] auto ntscf_v1_get_acf_payload(std::span<uint8_t const> packet) noexcept -> std::span<uint8_t const>;

}  // namespace statusbar::avtp
