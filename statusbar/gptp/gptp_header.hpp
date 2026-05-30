#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/buffer/buffer.hpp"
#include "statusbar/gptp/gptp_base.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/status/status.hpp"

#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace statusbar::gptp {

using ieee::doublet_t;
using ieee::octet_t;
using ieee::octlet_t;
using ieee::quadlet_t;
using statusbar::BufferError;
using statusbar::failure;
using statusbar::StatusValue;
using statusbar::success;

//
// MessageHeader (34 bytes)
// IEEE 802.1AS-2020 Clause 10.6.2.1 Table 10-7 - PTP message header
//
struct MessageHeader
{
    static constexpr size_t LENGTH = 34;

    // Byte 0: major_sdo_id (4 bits) | message_type (4 bits)
    octet_t sdo_id_message_type;
    // Byte 1: minor_version_ptp (4 bits) | version_ptp (4 bits)
    octet_t version;
    // Bytes 2-3: message length
    doublet_t message_length;
    // Byte 4: domain number
    octet_t domain_number;
    // Byte 5: minor_sdo_id
    octet_t minor_sdo_id;
    // Bytes 6-7: flags
    doublet_t flags;
    // Bytes 8-15: correction field (64-bit; signed per IEEE 1588, accessed via correction_field())
    octlet_t correction_field_raw;
    // Bytes 16-19: message type specific
    quadlet_t message_type_specific;
    // Bytes 20-29: source port identity (10 bytes)
    SourcePortIdentity source_port_identity;
    // Bytes 30-31: sequence ID
    doublet_t sequence_id;
    // Byte 32: control field
    octet_t control_field;
    // Byte 33: log message interval
    octet_t log_message_interval;

    constexpr MessageHeader() noexcept = default;

    // Packed bit-field accessors (byte 0 and byte 1)

    /// Get major SDO ID (upper 4 bits of byte 0)
    [[nodiscard]] constexpr auto major_sdo_id() const noexcept -> uint8_t { return sdo_id_message_type.get_bits<uint8_t>(0xF0, 4); }

    /// Set major SDO ID
    constexpr void set_major_sdo_id(uint8_t sdo_id) noexcept { sdo_id_message_type.set_bits(0xF0, 4, sdo_id); }

    /// Get message type (lower 4 bits of byte 0)
    [[nodiscard]] constexpr auto message_type() const noexcept -> uint8_t { return sdo_id_message_type.get_bits<uint8_t>(0x0F, 0); }

    /// Set message type
    constexpr void set_message_type(uint8_t msg_type) noexcept { sdo_id_message_type.set_bits(0x0F, 0, msg_type); }

    /// Get minor version PTP (upper 4 bits of byte 1)
    [[nodiscard]] constexpr auto minor_version_ptp() const noexcept -> uint8_t { return version.get_bits<uint8_t>(0xF0, 4); }

    /// Set minor version PTP
    constexpr void set_minor_version_ptp(uint8_t minor_ver) noexcept { version.set_bits(0xF0, 4, minor_ver); }

    /// Get version PTP (lower 4 bits of byte 1)
    [[nodiscard]] constexpr auto version_ptp() const noexcept -> uint8_t { return version.get_bits<uint8_t>(0x0F, 0); }

    /// Set version PTP
    constexpr void set_version_ptp(uint8_t ver) noexcept { version.set_bits(0x0F, 0, ver); }

    /// Get correction field. Per IEEE 1588 the correctionField is a signed 64-bit
    /// integer in 16.16 fixed-point nanoseconds; the wire value is stored in
    /// correction_field_raw (unsigned) and bit_cast to int64_t here.
    [[nodiscard]] constexpr auto correction_field() const noexcept -> int64_t
    {
        return std::bit_cast<int64_t>(static_cast<uint64_t>(correction_field_raw));
    }

    /// Set correction field (signed per IEEE 1588).
    constexpr void set_correction_field(int64_t cf) noexcept { correction_field_raw = std::bit_cast<uint64_t>(cf); }

    /// Initialize header for a typical gPTP message.
    /// Sets controlField per IEEE 1588-2008 Table 23: Event messages
    /// (Sync/Delay_Req/Pdelay_Req/Pdelay_Resp) → 5 except Sync=0 and
    /// Delay_Req=1; General messages → 5. For gPTP (802.1AS) the
    /// ptpTimescale flag (bit 3) is always set.
    constexpr void init(uint8_t msg_type, uint16_t msg_len, uint16_t seq_id = 0) noexcept
    {
        set_major_sdo_id(SDO_ID);
        set_message_type(msg_type);
        set_minor_version_ptp(0);
        set_version_ptp(VERSION_PTP);
        message_length = msg_len;
        domain_number = 0;
        minor_sdo_id = 0;
        // ptpTimescale (bit 3) is mandatory for 802.1AS
        flags = 0x0008;
        correction_field_raw = 0;
        message_type_specific = 0;
        source_port_identity = SourcePortIdentity{};
        sequence_id = seq_id;
        // controlField per IEEE 1588-2008 Table 23
        switch (msg_type) {
            case MESSAGE_TYPE_SYNC:
                control_field = 0;
                break;
            case MESSAGE_TYPE_DELAY_REQ:
                control_field = 1;
                break;
            case MESSAGE_TYPE_FOLLOW_UP:
                control_field = 2;
                break;
            case MESSAGE_TYPE_DELAY_RESP:
                control_field = 3;
                break;
            default:
                control_field = 5;
                break;  // All other including Pdelay_*
        }
        log_message_interval = 0;
    }

    /// Check if this is a Sync message
    [[nodiscard]] constexpr auto is_sync() const noexcept -> bool { return message_type() == MESSAGE_TYPE_SYNC; }

    /// Check if this is a Follow_Up message
    [[nodiscard]] constexpr auto is_follow_up() const noexcept -> bool { return message_type() == MESSAGE_TYPE_FOLLOW_UP; }

    /// Check if this is a Pdelay_Req message
    [[nodiscard]] constexpr auto is_pdelay_req() const noexcept -> bool { return message_type() == MESSAGE_TYPE_PDELAY_REQ; }

    /// Check if this is a Pdelay_Resp message
    [[nodiscard]] constexpr auto is_pdelay_resp() const noexcept -> bool { return message_type() == MESSAGE_TYPE_PDELAY_RESP; }

    /// Check if this is a Pdelay_Resp_Follow_Up message
    [[nodiscard]] constexpr auto is_pdelay_resp_follow_up() const noexcept -> bool
    {
        return message_type() == MESSAGE_TYPE_PDELAY_RESP_FOLLOW_UP;
    }

    /// Check if this is an Announce message
    [[nodiscard]] constexpr auto is_announce() const noexcept -> bool { return message_type() == MESSAGE_TYPE_ANNOUNCE; }

    /// Check if this is a Signaling message
    [[nodiscard]] constexpr auto is_signaling() const noexcept -> bool { return message_type() == MESSAGE_TYPE_SIGNALING; }

    /// Check if this is a Management message
    [[nodiscard]] constexpr auto is_management() const noexcept -> bool { return message_type() == MESSAGE_TYPE_MANAGEMENT; }

    // Validation

    /// Check if a message type value is valid per IEEE 802.1AS.
    /// Event messages: 0-3 (Sync, Delay_Req, Pdelay_Req, Pdelay_Resp)
    /// General messages: 8-13 (Follow_Up, Delay_Resp, Pdelay_Resp_Follow_Up, Announce, Signaling, Management)
    [[nodiscard]] static constexpr auto is_valid_message_type(uint8_t mt) noexcept -> bool
    {
        return (mt <= 3) || (mt >= 8 && mt <= 13);
    }

    /// Check if this is a valid gPTP message header
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool
    {
        if (major_sdo_id() != SDO_ID) {
            return false;
        }
        if (version_ptp() != VERSION_PTP) {
            return false;
        }
        if (!is_valid_message_type(message_type())) {
            return false;
        }
        if (message_length < LENGTH) {
            return false;
        }
        return true;
    }

    auto operator<=>(MessageHeader const& rhs) const noexcept -> std::strong_ordering = default;
};

// Compile-time layout verification
static_assert(sizeof(MessageHeader) == 34, "MessageHeader must be exactly 34 bytes");
static_assert(alignof(MessageHeader) <= 8, "MessageHeader alignment must not exceed 8 bytes");
static_assert(offsetof(MessageHeader, sdo_id_message_type) == 0, "sdo_id_message_type must be at offset 0");
static_assert(offsetof(MessageHeader, version) == 1, "version must be at offset 1");
static_assert(offsetof(MessageHeader, message_length) == 2, "message_length must be at offset 2");
static_assert(offsetof(MessageHeader, domain_number) == 4, "domain_number must be at offset 4");
static_assert(offsetof(MessageHeader, minor_sdo_id) == 5, "minor_sdo_id must be at offset 5");
static_assert(offsetof(MessageHeader, flags) == 6, "flags must be at offset 6");
static_assert(offsetof(MessageHeader, correction_field_raw) == 8, "correction_field_raw must be at offset 8");
static_assert(offsetof(MessageHeader, message_type_specific) == 16, "message_type_specific must be at offset 16");
static_assert(offsetof(MessageHeader, source_port_identity) == 20, "source_port_identity must be at offset 20");
static_assert(offsetof(MessageHeader, sequence_id) == 30, "sequence_id must be at offset 30");
static_assert(offsetof(MessageHeader, control_field) == 32, "control_field must be at offset 32");
static_assert(offsetof(MessageHeader, log_message_interval) == 33, "log_message_interval must be at offset 33");

}  // namespace statusbar::gptp

// Serialization traits - MessageHeader is a packed wire format struct
template <>
struct statusbar::traits::is_serializable_wire_fixed_struct<statusbar::gptp::MessageHeader> : std::true_type
{};

// Export using declarations for ADL to find the template functions
namespace statusbar::gptp {
using protocol::load_unchecked;
using protocol::store_unchecked;
}  // namespace statusbar::gptp
