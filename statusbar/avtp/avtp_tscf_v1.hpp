#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// TSCF Version 1 - Time-Synchronous Control Format with version 1 common stream header
/// IEEE 1722-2025 Clause 9.3 with version 1 header per 4.7.4
///
/// Version 1 improvements over version 0:
///   - sequence_num: 32-bit (was 8-bit)
///   - avtp_timestamp: 64-bit (was 32-bit)
///   - ptp_grandmaster_identity: 8 bytes - clock domain identification
///
/// Wire format version 1 (40-byte header + variable acf_payload_data), per Figure 61:
///   Byte 0:      subtype (0x05)
///   Byte 1:      sv[7] | version=1[6:4] | mr[3] | rsv[2:1] | tv[0]
///   Byte 2:      format_specific_data_0 = sequence_num_lsb (low 8 bits of sequence_num)
///   Byte 3:      reserved[7:1] | tu[0]
///   Bytes 4-11:  stream_id (8 bytes)
///   Bytes 12-15: sequence_num (32-bit)
///   Bytes 16-23: avtp_timestamp (64-bit PTP nanoseconds)
///   Bytes 24-31: ptp_grandmaster_identity (ClockIdentity, 8 bytes)
///   Bytes 32-35: reserved (format_specific_data_2)
///   Bytes 36-37: stream_data_length (acf payload length in octets)
///   Bytes 38-39: reserved (format_specific_data_3)
///   Bytes 40+:   acf_payload_data

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
using ieee::octlet_t;
using ieee::quadlet_t;
using tsn::ClockIdentity;
using tsn::StreamId;

//
// TscfV1Pdu - IEEE 1722-2025 Clause 9.3, Figure 61 (version 1)
// Based on AVTPDU common stream header version 1 (Figure 11)
//

/// TSCF Protocol Data Unit Header version 1 - IEEE 1722-2025 Clause 9.3 (subtype 0x05, version 1)
struct TscfV1Pdu
{
    /// Total length of TSCF V1 header on wire
    static constexpr size_t LENGTH = 40;
    static constexpr size_t HEADER_LENGTH = LENGTH;

    // Bytes 0-3: common header fields

    /// Byte 0: subtype[7:0] = 0x05 for TSCF
    octet_t subtype;

    /// Byte 1: sv[7] | version[6:4] | mr[3] | rsv[2:1] | tv[0]
    octet_t sv_version_flags;

    /// Byte 2: format_specific_data_0 = sequence_num_lsb (per 9.3.2)
    octet_t sequence_num_lsb;

    /// Byte 3: reserved[7:1] | tu[0]
    octet_t reserved_tu;

    // Bytes 4-11: Stream ID

    /// Bytes 4-11: Stream ID
    StreamId stream_id_;

    // Bytes 12-15: sequence_num (32-bit, version 1)

    /// Bytes 12-15: sequence_num (32-bit)
    quadlet_t sequence_num;

    // Bytes 16-23: avtp_timestamp (64-bit, version 1)

    /// Bytes 16-23: avtp_timestamp (64-bit PTP nanoseconds)
    octlet_t avtp_timestamp;

    // Bytes 24-31: ptp_grandmaster_identity

    /// Bytes 24-31: PTP grandmaster ClockIdentity
    ClockIdentity ptp_grandmaster_identity;

    // Bytes 32-35: reserved (format_specific_data_2)

    /// Bytes 32-35: reserved
    quadlet_t reserved1;

    // Bytes 36-37: stream_data_length

    /// Bytes 36-37: stream_data_length (acf payload length in octets)
    doublet_t stream_data_length;

    // Bytes 38-39: reserved (format_specific_data_3)

    /// Bytes 38-39: reserved
    doublet_t reserved2;

    // ========== Accessors - Common stream header flags (byte 1) ==========

    /// Get the stream valid (sv) bit
    [[nodiscard]] constexpr auto sv() const noexcept -> bool { return sv_version_flags.has_flag(0x80U); }

    /// Set the stream valid (sv) bit
    constexpr void set_sv(bool const value) noexcept { sv_version_flags.set_flag(0x80U, value); }

    /// Get the version field (bits 6:4) — should be 1 for this struct
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

    // ========== Accessors - Stream ID ==========

    /// Get the stream ID
    [[nodiscard]] constexpr auto stream_id() const noexcept -> StreamId { return stream_id_; }

    /// Set the stream ID
    constexpr void set_stream_id(StreamId const& sid) noexcept { stream_id_ = sid; }

    // ========== Accessors - Version 1 specific fields ==========

    /// Get the sequence number (32-bit)
    [[nodiscard]] constexpr auto get_sequence_num() const noexcept -> uint32_t { return sequence_num.get(); }

    /// Set the sequence number (also updates sequence_num_lsb per 9.3.2)
    constexpr void set_sequence_num(uint32_t const value) noexcept
    {
        sequence_num = value;
        sequence_num_lsb = static_cast<uint8_t>(value & 0xFFU);
    }

    /// Increment sequence number (wraps at 2^32, updates lsb)
    constexpr void increment_sequence_num() noexcept { set_sequence_num(get_sequence_num() + 1U); }

    /// Get the AVTP timestamp (64-bit PTP nanoseconds, valid when tv==1)
    [[nodiscard]] constexpr auto get_avtp_timestamp() const noexcept -> uint64_t { return avtp_timestamp.get(); }

    /// Set the AVTP timestamp (64-bit)
    constexpr void set_avtp_timestamp(uint64_t const value) noexcept { avtp_timestamp = value; }

    /// Get the PTP grandmaster identity (valid when tv==1)
    [[nodiscard]] constexpr auto get_ptp_grandmaster_identity() const noexcept -> ClockIdentity { return ptp_grandmaster_identity; }

    /// Set the PTP grandmaster identity
    constexpr void set_ptp_grandmaster_identity(ClockIdentity const& gm) noexcept { ptp_grandmaster_identity = gm; }

    // ========== Accessors - Payload length ==========

    /// Get the stream_data_length (acf_payload_data length in octets)
    [[nodiscard]] constexpr auto get_stream_data_length() const noexcept -> uint16_t { return stream_data_length.get(); }

    /// Set the stream_data_length
    constexpr void set_stream_data_length(uint16_t const len) noexcept { stream_data_length = len; }

    // ========== Initialization ==========

    /// Initialize for TSCF with version 1 header
    constexpr void init(StreamId const& sid, bool const sv_valid = true) noexcept
    {
        subtype = AvtpSubtype::tscf;
        sv_version_flags = 0x10U;  // version=1
        set_sv(sv_valid);
        sequence_num_lsb = 0U;
        reserved_tu = 0U;
        stream_id_ = sid;
        sequence_num = 0U;
        avtp_timestamp = 0U;
        ptp_grandmaster_identity = ClockIdentity{};
        reserved1 = 0U;
        stream_data_length = 0U;
        reserved2 = 0U;
    }

    // ========== Validation ==========

    /// Check if this is a valid TSCF version 1 header
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool
    {
        if (subtype != AvtpSubtype::tscf) {
            return false;
        }
        if (version() != 1) {
            return false;
        }
        return true;
    }

    auto operator<=>(TscfV1Pdu const& rhs) const noexcept -> std::strong_ordering = default;
};

// Compile-time layout verification
static_assert(sizeof(TscfV1Pdu) == 40, "TscfV1Pdu must be exactly 40 bytes");
static_assert(alignof(TscfV1Pdu) <= 4, "TscfV1Pdu alignment must not exceed 4 bytes");
static_assert(offsetof(TscfV1Pdu, subtype) == 0, "subtype must be at offset 0");
static_assert(offsetof(TscfV1Pdu, sv_version_flags) == 1, "sv_version_flags must be at offset 1");
static_assert(offsetof(TscfV1Pdu, sequence_num_lsb) == 2, "sequence_num_lsb must be at offset 2");
static_assert(offsetof(TscfV1Pdu, reserved_tu) == 3, "reserved_tu must be at offset 3");
static_assert(offsetof(TscfV1Pdu, stream_id_) == 4, "stream_id must be at offset 4");
static_assert(offsetof(TscfV1Pdu, sequence_num) == 12, "sequence_num must be at offset 12");
static_assert(offsetof(TscfV1Pdu, avtp_timestamp) == 16, "avtp_timestamp must be at offset 16");
static_assert(offsetof(TscfV1Pdu, ptp_grandmaster_identity) == 24, "ptp_grandmaster_identity must be at offset 24");
static_assert(offsetof(TscfV1Pdu, reserved1) == 32, "reserved1 must be at offset 32");
static_assert(offsetof(TscfV1Pdu, stream_data_length) == 36, "stream_data_length must be at offset 36");
static_assert(offsetof(TscfV1Pdu, reserved2) == 38, "reserved2 must be at offset 38");

}  // namespace statusbar::avtp

// Serialization trait
template <>
struct statusbar::traits::is_serializable_wire_fixed_struct<statusbar::avtp::TscfV1Pdu> : std::true_type
{};

namespace statusbar::avtp {

using protocol::load_unchecked;
using protocol::store_unchecked;

//
// Parse/create helpers
//

/// Parse a TSCF V1 header from a packet
/// @param packet Raw packet data (must have subtype==TSCF and version==1)
[[nodiscard]] auto tscf_v1_parse_header(std::span<uint8_t const> packet) noexcept -> std::optional<TscfV1Pdu>;

/// Get ACF payload data span from a TSCF V1 packet
/// @param packet Raw packet data including the TSCF V1 header
[[nodiscard]] auto tscf_v1_get_acf_payload(std::span<uint8_t const> packet) noexcept -> std::span<uint8_t const>;

}  // namespace statusbar::avtp
