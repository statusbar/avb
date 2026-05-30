#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// TSCF - Time-Synchronous Control Format - IEEE 1722-2025 Clause 9.3
/// Common stream header for time-synchronous ACF control messages.
///
/// Wire format version 0 (24-byte header + variable acf_payload_data):
///   Byte 0:      subtype (0x05)
///   Byte 1:      sv[7] | version[6:4] | mr[3] | rsv[2:1] | tv[0]
///   Byte 2:      sequence_num[7:0]
///   Byte 3:      rsv[7:1] | tu[0]
///   Bytes 4-11:  stream_id (8 bytes)
///   Bytes 12-15: avtp_timestamp (32-bit PTP nanoseconds)
///   Bytes 16-17: rsv[15:8] | sequence_num_lsb[7:0]
///   Bytes 18-19: reserved
///   Bytes 20-21: stream_data_length (acf payload length in octets)
///   Bytes 22-23: reserved
///   Bytes 24+:   acf_payload_data

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
using ieee::quadlet_t;
using tsn::StreamId;

//
// TscfPdu - IEEE 1722-2025 Clause 9.3, Figure 60 (version 0)
// Based on AVTPDU common stream header version 0 (Figure 10)
//

/// TSCF Protocol Data Unit Header - IEEE 1722-2025 Clause 9.3 (subtype 0x05)
struct TscfPdu
{
    /// Total length of TSCF header on wire (version 0)
    static constexpr size_t LENGTH = 24;
    static constexpr size_t HEADER_LENGTH = LENGTH;

    // Byte 0: subtype[7:0] = 0x05 for TSCF
    octet_t subtype;

    // Byte 1: sv[7] | version[6:4] | mr[3] | rsv[2:1] | tv[0]
    octet_t sv_version_flags;

    // Byte 2: sequence_num[7:0]
    octet_t sequence_num;

    // Byte 3: reserved[7:1] | tu[0]
    octet_t reserved_tu;

    // Bytes 4-11: stream_id
    StreamId stream_id_;

    // Bytes 12-15: avtp_timestamp
    quadlet_t avtp_timestamp;

    // Byte 16: reserved
    octet_t reserved1;

    // Byte 17: sequence_num_lsb (copy of sequence_num low 8 bits, per 9.3.2)
    octet_t sequence_num_lsb;

    // Bytes 18-19: reserved
    doublet_t reserved2;

    // Bytes 20-21: stream_data_length (acf payload length in octets)
    doublet_t stream_data_length;

    // Bytes 22-23: reserved
    doublet_t reserved3;

    // Accessors - Control flags (byte 1)

    /// Get the stream valid (sv) bit
    [[nodiscard]] constexpr auto sv() const noexcept -> bool { return sv_version_flags.has_flag(0x80U); }

    /// Set the stream valid (sv) bit
    constexpr void set_sv(bool const value) noexcept { sv_version_flags.set_flag(0x80U, value); }

    /// Get the version field (bits 6:4)
    [[nodiscard]] constexpr auto version() const noexcept -> uint8_t { return sv_version_flags.get_bits(0x70U, 4); }

    /// Get the media clock restart (mr) bit
    [[nodiscard]] constexpr auto mr() const noexcept -> bool { return sv_version_flags.has_flag(0x08U); }

    /// Set the media clock restart (mr) bit
    constexpr void set_mr(bool const value) noexcept { sv_version_flags.set_flag(0x08U, value); }

    /// Get the timestamp valid (tv) bit
    [[nodiscard]] constexpr auto tv() const noexcept -> bool { return sv_version_flags.has_flag(0x01U); }

    /// Set the timestamp valid (tv) bit
    constexpr void set_tv(bool const value) noexcept { sv_version_flags.set_flag(0x01U, value); }

    /// Get the timestamp uncertain (tu) bit
    [[nodiscard]] constexpr auto tu() const noexcept -> bool { return reserved_tu.has_flag(0x01U); }

    /// Set the timestamp uncertain (tu) bit
    constexpr void set_tu(bool const value) noexcept { reserved_tu.set_flag(0x01U, value); }

    // Accessors - Stream ID

    /// Get the stream ID
    [[nodiscard]] constexpr auto stream_id() const noexcept -> StreamId { return stream_id_; }

    /// Set the stream ID
    constexpr void set_stream_id(StreamId const& sid) noexcept { stream_id_ = sid; }

    // Accessors - Timestamp

    /// Get the AVTP timestamp (32-bit PTP nanoseconds, valid when tv==1)
    [[nodiscard]] constexpr auto get_avtp_timestamp() const noexcept -> uint32_t { return avtp_timestamp.get(); }

    /// Set the AVTP timestamp
    constexpr void set_avtp_timestamp(uint32_t const ts) noexcept { avtp_timestamp = ts; }

    // Accessors - Sequence number

    /// Get the sequence number (8-bit, version 0)
    [[nodiscard]] constexpr auto get_sequence_num() const noexcept -> uint8_t { return sequence_num.get(); }

    /// Set the sequence number (also updates sequence_num_lsb per 9.3.2)
    constexpr void set_sequence_num(uint8_t const seq) noexcept
    {
        sequence_num = seq;
        sequence_num_lsb = seq;
    }

    // Accessors - Payload length

    /// Get the stream_data_length (acf_payload_data length in octets)
    [[nodiscard]] constexpr auto get_stream_data_length() const noexcept -> uint16_t { return stream_data_length.get(); }

    /// Set the stream_data_length
    constexpr void set_stream_data_length(uint16_t const len) noexcept { stream_data_length = len; }

    // Initialization

    /// Initialize for TSCF
    /// @param sid Stream ID
    /// @param sv_valid Whether stream_id has a valid reservation (sv bit)
    constexpr void init(StreamId const& sid, bool const sv_valid = true) noexcept
    {
        subtype = AvtpSubtype::tscf;
        sv_version_flags = 0U;
        set_sv(sv_valid);
        sequence_num = 0U;
        reserved_tu = 0U;
        stream_id_ = sid;
        avtp_timestamp = 0U;
        reserved1 = 0U;
        sequence_num_lsb = 0U;
        reserved2 = 0U;
        stream_data_length = 0U;
        reserved3 = 0U;
    }

    // Validation

    /// Check if this is a valid TSCF header
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool
    {
        if (subtype != AvtpSubtype::tscf) {
            return false;
        }
        if (version() != 0) {
            return false;
        }
        return true;
    }

    auto operator<=>(TscfPdu const& rhs) const noexcept -> std::strong_ordering = default;
};

// Compile-time layout verification
static_assert(sizeof(TscfPdu) == 24, "TscfPdu must be exactly 24 bytes");
static_assert(alignof(TscfPdu) <= 4, "TscfPdu alignment must not exceed 4 bytes");
static_assert(offsetof(TscfPdu, subtype) == 0, "subtype must be at offset 0");
static_assert(offsetof(TscfPdu, sv_version_flags) == 1, "sv_version_flags must be at offset 1");
static_assert(offsetof(TscfPdu, sequence_num) == 2, "sequence_num must be at offset 2");
static_assert(offsetof(TscfPdu, reserved_tu) == 3, "reserved_tu must be at offset 3");
static_assert(offsetof(TscfPdu, stream_id_) == 4, "stream_id must be at offset 4");
static_assert(offsetof(TscfPdu, avtp_timestamp) == 12, "avtp_timestamp must be at offset 12");
static_assert(offsetof(TscfPdu, reserved1) == 16, "reserved1 must be at offset 16");
static_assert(offsetof(TscfPdu, sequence_num_lsb) == 17, "sequence_num_lsb must be at offset 17");
static_assert(offsetof(TscfPdu, reserved2) == 18, "reserved2 must be at offset 18");
static_assert(offsetof(TscfPdu, stream_data_length) == 20, "stream_data_length must be at offset 20");
static_assert(offsetof(TscfPdu, reserved3) == 22, "reserved3 must be at offset 22");

}  // namespace statusbar::avtp

// Serialization trait
template <>
struct statusbar::traits::is_serializable_wire_fixed_struct<statusbar::avtp::TscfPdu> : std::true_type
{};

namespace statusbar::avtp {

using protocol::load_unchecked;
using protocol::store_unchecked;

//
// Parse/create helpers
//

/// Parse a TSCF header from a packet
/// @param packet Raw packet data (Ethernet payload starting at subtype byte)
[[nodiscard]] auto tscf_parse_header(std::span<uint8_t const> packet) noexcept -> std::optional<TscfPdu>;

/// Get ACF payload data span from a TSCF packet
/// @param packet Raw packet data including the TSCF header
[[nodiscard]] auto tscf_get_acf_payload(std::span<uint8_t const> packet) noexcept -> std::span<uint8_t const>;

}  // namespace statusbar::avtp
