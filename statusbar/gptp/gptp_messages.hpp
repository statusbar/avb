#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/buffer/buffer.hpp"
#include "statusbar/gptp/gptp_base.hpp"
#include "statusbar/gptp/gptp_header.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/status/status.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <type_traits>
#include <variant>

namespace statusbar::gptp {

using statusbar::BufferError;
using statusbar::failure;
using statusbar::StatusValue;
using statusbar::success;

//
// SyncMessage (44 bytes = 34 byte header + 10 byte timestamp)
// IEEE 802.1AS-2020 Clause 10.6.2.2.1
//
struct SyncMessage
{
    static constexpr size_t LENGTH = 44;

    MessageHeader header;
    Timestamp origin_timestamp;

    constexpr SyncMessage() noexcept = default;

    /// Initialize a Sync message
    constexpr void init(uint16_t seq_id = 0) noexcept
    {
        header.init(MESSAGE_TYPE_SYNC, LENGTH, seq_id);
        origin_timestamp = Timestamp{};
    }

    auto operator<=>(SyncMessage const& rhs) const noexcept -> std::strong_ordering = default;
};

static_assert(sizeof(SyncMessage) == 44, "SyncMessage must be exactly 44 bytes");
static_assert(offsetof(SyncMessage, header) == 0, "header must be at offset 0");
static_assert(offsetof(SyncMessage, origin_timestamp) == 34, "origin_timestamp must be at offset 34");

//
// FollowUpMessage (44 bytes = 34 byte header + 10 byte timestamp)
// IEEE 802.1AS-2020 Clause 10.6.2.2.2
//
struct FollowUpMessage
{
    static constexpr size_t LENGTH = 44;

    MessageHeader header;
    Timestamp precise_origin_timestamp;

    constexpr FollowUpMessage() noexcept = default;

    /// Initialize a Follow_Up message
    constexpr void init(uint16_t seq_id = 0) noexcept
    {
        header.init(MESSAGE_TYPE_FOLLOW_UP, LENGTH, seq_id);
        precise_origin_timestamp = Timestamp{};
    }

    auto operator<=>(FollowUpMessage const& rhs) const noexcept -> std::strong_ordering = default;
};

static_assert(sizeof(FollowUpMessage) == 44, "FollowUpMessage must be exactly 44 bytes");
static_assert(offsetof(FollowUpMessage, header) == 0, "header must be at offset 0");
static_assert(offsetof(FollowUpMessage, precise_origin_timestamp) == 34, "precise_origin_timestamp must be at offset 34");

//
// PdelayReqMessage (54 bytes = 34 byte header + 10 byte timestamp + 10 byte reserved)
// IEEE 802.1AS-2020 Clause 10.6.2.2.3
//
struct PdelayReqMessage
{
    static constexpr size_t LENGTH = 54;

    MessageHeader header{};
    Timestamp origin_timestamp{};
    std::array<uint8_t, 10> reserved{};

    constexpr PdelayReqMessage() noexcept = default;

    /// Initialize a Pdelay_Req message
    constexpr void init(uint16_t seq_id = 0) noexcept
    {
        header.init(MESSAGE_TYPE_PDELAY_REQ, LENGTH, seq_id);
        origin_timestamp = Timestamp{};
        reserved = {};
    }

    auto operator<=>(PdelayReqMessage const& rhs) const noexcept -> std::strong_ordering = default;
};

static_assert(sizeof(PdelayReqMessage) == 54, "PdelayReqMessage must be exactly 54 bytes");
static_assert(offsetof(PdelayReqMessage, header) == 0, "header must be at offset 0");
static_assert(offsetof(PdelayReqMessage, origin_timestamp) == 34, "origin_timestamp must be at offset 34");
static_assert(offsetof(PdelayReqMessage, reserved) == 44, "reserved must be at offset 44");

//
// PdelayRespMessage (54 bytes = 34 byte header + 10 byte timestamp + 10 byte requesting port)
// IEEE 802.1AS-2020 Clause 10.6.2.2.4
//
struct PdelayRespMessage
{
    static constexpr size_t LENGTH = 54;

    MessageHeader header;
    Timestamp request_receipt_timestamp;
    SourcePortIdentity requesting_port_identity;

    constexpr PdelayRespMessage() noexcept = default;

    /// Initialize a Pdelay_Resp message
    constexpr void init(uint16_t seq_id = 0) noexcept
    {
        header.init(MESSAGE_TYPE_PDELAY_RESP, LENGTH, seq_id);
        request_receipt_timestamp = Timestamp{};
        requesting_port_identity = SourcePortIdentity{};
    }

    auto operator<=>(PdelayRespMessage const& rhs) const noexcept -> std::strong_ordering = default;
};

static_assert(sizeof(PdelayRespMessage) == 54, "PdelayRespMessage must be exactly 54 bytes");
static_assert(offsetof(PdelayRespMessage, header) == 0, "header must be at offset 0");
static_assert(offsetof(PdelayRespMessage, request_receipt_timestamp) == 34, "request_receipt_timestamp must be at offset 34");
static_assert(offsetof(PdelayRespMessage, requesting_port_identity) == 44, "requesting_port_identity must be at offset 44");

//
// PdelayRespFollowUpMessage (54 bytes = 34 byte header + 10 byte timestamp + 10 byte requesting port)
// IEEE 802.1AS-2020 Clause 10.6.2.2.5
//
struct PdelayRespFollowUpMessage
{
    static constexpr size_t LENGTH = 54;

    MessageHeader header;
    Timestamp response_origin_timestamp;
    SourcePortIdentity requesting_port_identity;

    constexpr PdelayRespFollowUpMessage() noexcept = default;

    /// Initialize a Pdelay_Resp_Follow_Up message
    constexpr void init(uint16_t seq_id = 0) noexcept
    {
        header.init(MESSAGE_TYPE_PDELAY_RESP_FOLLOW_UP, LENGTH, seq_id);
        response_origin_timestamp = Timestamp{};
        requesting_port_identity = SourcePortIdentity{};
    }

    auto operator<=>(PdelayRespFollowUpMessage const& rhs) const noexcept -> std::strong_ordering = default;
};

static_assert(sizeof(PdelayRespFollowUpMessage) == 54, "PdelayRespFollowUpMessage must be exactly 54 bytes");
static_assert(offsetof(PdelayRespFollowUpMessage, header) == 0, "header must be at offset 0");
static_assert(
    offsetof(PdelayRespFollowUpMessage, response_origin_timestamp) == 34, "response_origin_timestamp must be at offset 34");
static_assert(offsetof(PdelayRespFollowUpMessage, requesting_port_identity) == 44, "requesting_port_identity must be at offset 44");

//
// AnnounceMessage (64 bytes = 34 byte header + 30 byte announce data)
// IEEE 802.1AS-2020 Clause 10.5.3 - Announce message
//
struct AnnounceMessage
{
    static constexpr size_t LENGTH = 64;

    MessageHeader header;
    // Bytes 34-43: origin timestamp (reserved, set to 0 in 802.1AS)
    Timestamp origin_timestamp;
    // Bytes 44-45: currentUtcOffset
    doublet_t current_utc_offset;
    // Byte 46: reserved
    octet_t reserved;
    // Byte 47: grandmasterPriority1
    octet_t grandmaster_priority1;
    // Bytes 48-51: grandmasterClockQuality
    ClockQuality grandmaster_clock_quality;
    // Byte 52: grandmasterPriority2
    octet_t grandmaster_priority2;
    // Bytes 53-60: grandmasterIdentity (8 bytes)
    ClockIdentity grandmaster_identity;
    // Bytes 61-62: stepsRemoved
    doublet_t steps_removed;
    // Byte 63: timeSource
    octet_t time_source;

    constexpr AnnounceMessage() noexcept = default;

    /// Initialize an Announce message
    constexpr void init(uint16_t seq_id = 0) noexcept
    {
        header.init(MESSAGE_TYPE_ANNOUNCE, LENGTH, seq_id);
        origin_timestamp = Timestamp{};
        current_utc_offset = 0;
        reserved = 0;
        grandmaster_priority1 = 255;
        grandmaster_clock_quality = ClockQuality{};
        grandmaster_priority2 = 255;
        grandmaster_identity = ClockIdentity{};
        steps_removed = 0;
        time_source = 0;
    }

    auto operator<=>(AnnounceMessage const& rhs) const noexcept -> std::strong_ordering = default;
};

static_assert(sizeof(AnnounceMessage) == 64, "AnnounceMessage must be exactly 64 bytes");
static_assert(offsetof(AnnounceMessage, header) == 0, "header must be at offset 0");
static_assert(offsetof(AnnounceMessage, origin_timestamp) == 34, "origin_timestamp must be at offset 34");
static_assert(offsetof(AnnounceMessage, current_utc_offset) == 44, "current_utc_offset must be at offset 44");
static_assert(offsetof(AnnounceMessage, reserved) == 46, "reserved must be at offset 46");
static_assert(offsetof(AnnounceMessage, grandmaster_priority1) == 47, "grandmaster_priority1 must be at offset 47");
static_assert(offsetof(AnnounceMessage, grandmaster_clock_quality) == 48, "grandmaster_clock_quality must be at offset 48");
static_assert(offsetof(AnnounceMessage, grandmaster_priority2) == 52, "grandmaster_priority2 must be at offset 52");
static_assert(offsetof(AnnounceMessage, grandmaster_identity) == 53, "grandmaster_identity must be at offset 53");
static_assert(offsetof(AnnounceMessage, steps_removed) == 61, "steps_removed must be at offset 61");
static_assert(offsetof(AnnounceMessage, time_source) == 63, "time_source must be at offset 63");

//
// SignalingMessage (44 byte fixed header, variable-length TLV suffix)
// IEEE 802.1AS-2020 Clause 10.6.4.
//
// The fixed portion is a 34-byte common header + a 10-byte
// targetPortIdentity. TLVs follow immediately after and are parsed
// separately (see gptp_tlv.hpp). The wire `messageLength` field in
// the header determines the total size including TLVs.
//
struct SignalingMessage
{
    static constexpr size_t FIXED_LENGTH = 44;
    /// Alias used by the protocol buffer helpers (load_unchecked /
    /// store_unchecked look for `T::LENGTH`). We serialize only the
    /// fixed portion here; TLV suffixes are handled separately.
    static constexpr size_t LENGTH = FIXED_LENGTH;

    MessageHeader header;
    SourcePortIdentity target_port_identity;

    constexpr SignalingMessage() noexcept = default;

    /// Initialize a Signaling message with the given sequence id and
    /// an initial message_length of FIXED_LENGTH (caller should add
    /// TLV bytes and update the header's message_length accordingly).
    constexpr void init(uint16_t seq_id = 0) noexcept
    {
        header.init(MESSAGE_TYPE_SIGNALING, FIXED_LENGTH, seq_id);
        target_port_identity = SourcePortIdentity{};
    }

    auto operator<=>(SignalingMessage const& rhs) const noexcept -> std::strong_ordering = default;
};

static_assert(sizeof(SignalingMessage) == 44, "SignalingMessage fixed portion must be 44 bytes");
static_assert(offsetof(SignalingMessage, header) == 0);
static_assert(offsetof(SignalingMessage, target_port_identity) == 34);

//
// GptpMessage - Variant type for parsed gPTP messages
//
/// Represents a truncated gPTP message (header only, payload too short)
struct GptpTruncated
{
    MessageHeader header;
};

/// Variant holding all possible gPTP message types
using GptpMessage = std::variant<
    GptpTruncated,
    SyncMessage,
    FollowUpMessage,
    PdelayReqMessage,
    PdelayRespMessage,
    PdelayRespFollowUpMessage,
    AnnounceMessage,
    SignalingMessage,
    MessageHeader>;

/// Parse a gPTP message from payload
/// Returns std::nullopt if payload is too short for even a header
[[nodiscard]] auto parse_gptp(std::span<uint8_t const> payload) -> std::optional<GptpMessage>;

}  // namespace statusbar::gptp

// Serialization traits - gPTP message structs are packed wire format
template <>
struct statusbar::traits::is_serializable_wire_fixed_struct<statusbar::gptp::SyncMessage> : std::true_type
{};

template <>
struct statusbar::traits::is_serializable_wire_fixed_struct<statusbar::gptp::FollowUpMessage> : std::true_type
{};

template <>
struct statusbar::traits::is_serializable_wire_fixed_struct<statusbar::gptp::PdelayReqMessage> : std::true_type
{};

template <>
struct statusbar::traits::is_serializable_wire_fixed_struct<statusbar::gptp::PdelayRespMessage> : std::true_type
{};

template <>
struct statusbar::traits::is_serializable_wire_fixed_struct<statusbar::gptp::PdelayRespFollowUpMessage> : std::true_type
{};

template <>
struct statusbar::traits::is_serializable_wire_fixed_struct<statusbar::gptp::AnnounceMessage> : std::true_type
{};

template <>
struct statusbar::traits::is_serializable_wire_fixed_struct<statusbar::gptp::SignalingMessage> : std::true_type
{};

// Export using declarations for ADL to find the template functions
namespace statusbar::gptp {
using protocol::load_unchecked;
using protocol::store_unchecked;
}  // namespace statusbar::gptp
