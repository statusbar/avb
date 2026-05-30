#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// AM824 Version 1 - IEC 61883-6 Audio over IEEE 1722 AVTP with version 1 common stream header
/// IEEE 1722-2025 Clause 5.4 with version 1 header per 4.7.4
///
/// Version 1 improvements over version 0:
///   - sequence_num: 32-bit (was 8-bit) - reliable loss detection over lossy links
///   - avtp_timestamp: 64-bit (was 32-bit) - no 4-second rollover, no reconstruction needed
///   - ptp_grandmaster_identity: 8 bytes - clock domain identification at the transport layer
///
/// Wire format (40-byte AVTP stream header + 8-byte CIP header + variable audio payload):
///   AvtpStreamHeaderV1 (40 bytes):
///     Byte 0:      subtype (0x00)
///     Byte 1:      sv[7] | version=1[6:4] | mr[3] | r[2] | gv[1] | tv[0]
///     Byte 2:      format_specific_data_0 (reserved for 61883)
///     Byte 3:      reserved[7:1] | tu[0]
///     Bytes 4-11:  stream_id (8 bytes)
///     Bytes 12-15: sequence_num (32-bit)
///     Bytes 16-23: avtp_timestamp (64-bit PTP nanoseconds)
///     Bytes 24-31: ptp_grandmaster_identity (ClockIdentity, 8 bytes)
///     Bytes 32-35: gateway_info (format_specific_data_2)
///     Bytes 36-37: stream_data_length
///     Bytes 38-39: protocol_specific_header (tag[15:14] | channel[13:8] | tcode[7:4] | sy[3:0])
///   Cip61883Header (8 bytes):
///     Bytes 40-47: CIP header (reused from avtp_am824.hpp)

#include "statusbar/avtp/avtp_am824.hpp"
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
// AvtpStreamHeaderV1 - IEEE 1722-2025 Clause 5.4, Figure 11 (version 1)
// Common stream header for IEC 61883/IIDC with version 1 extensions
//

/// AVTP Common Stream Header version 1 - 40 bytes
struct AvtpStreamHeaderV1
{
    /// Total length of common stream header V1 on wire
    static constexpr size_t LENGTH = 40;

    // Bytes 0-3: subtype, sv, version, flags

    /// Byte 0: subtype[7:0] = 0x00 for IEC 61883/IIDC
    octet_t subtype;

    /// Byte 1: sv[7] | version[6:4] | mr[3] | r[2] | gv[1] | tv[0]
    octet_t sv_version_flags;

    /// Byte 2: format_specific_data_0 (reserved for 61883)
    octet_t format_specific_data_0;

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

    // Bytes 32-35: format_specific_data_2 (gateway_info for 61883)

    /// Bytes 32-35: gateway_info
    quadlet_t gateway_info;

    // Bytes 36-37: stream_data_length

    /// Bytes 36-37: stream_data_length (payload length in octets)
    doublet_t stream_data_length;

    // Bytes 38-39: protocol_specific_header

    /// Bytes 38-39: protocol_specific_header (tag[15:14] | channel[13:8] | tcode[7:4] | sy[3:0])
    doublet_t protocol_specific_header;

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

    /// Get the gateway valid (gv) bit
    [[nodiscard]] constexpr auto gv() const noexcept -> bool { return sv_version_flags.has_flag(0x02U); }

    /// Set the gateway valid (gv) bit
    constexpr void set_gv(bool const value) noexcept { sv_version_flags.set_flag(0x02U, value); }

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

    /// Get the sequence number (32-bit, version 1)
    [[nodiscard]] constexpr auto get_sequence_num() const noexcept -> uint32_t { return sequence_num.get(); }

    /// Set the sequence number (32-bit)
    constexpr void set_sequence_num(uint32_t const value) noexcept { sequence_num = value; }

    /// Increment sequence number (wraps at 2^32)
    constexpr void increment_sequence_num() noexcept { sequence_num = get_sequence_num() + 1U; }

    /// Get the AVTP timestamp (64-bit PTP nanoseconds, valid when tv==1)
    [[nodiscard]] constexpr auto get_avtp_timestamp() const noexcept -> uint64_t { return avtp_timestamp.get(); }

    /// Set the AVTP timestamp (64-bit)
    constexpr void set_avtp_timestamp(uint64_t const value) noexcept { avtp_timestamp = value; }

    /// Get the PTP grandmaster identity (valid when tv==1)
    [[nodiscard]] constexpr auto get_ptp_grandmaster_identity() const noexcept -> ClockIdentity { return ptp_grandmaster_identity; }

    /// Set the PTP grandmaster identity
    constexpr void set_ptp_grandmaster_identity(ClockIdentity const& gm) noexcept { ptp_grandmaster_identity = gm; }

    // ========== Accessors - stream_data_length ==========

    /// Get stream_data_length (payload length in octets)
    [[nodiscard]] constexpr auto get_stream_data_length() const noexcept -> uint16_t { return stream_data_length.get(); }

    /// Set stream_data_length
    constexpr void set_stream_data_length(uint16_t const length) noexcept { stream_data_length = length; }

    // ========== Accessors - protocol_specific_header (61883/IIDC fields) ==========

    /// Get tag field (bits 15:14 of protocol_specific_header)
    [[nodiscard]] constexpr auto tag() const noexcept -> uint8_t
    {
        return static_cast<uint8_t>((protocol_specific_header.get() >> 14) & 0x03U);
    }

    /// Get channel field (bits 13:8 of protocol_specific_header)
    [[nodiscard]] constexpr auto channel() const noexcept -> uint8_t
    {
        return static_cast<uint8_t>((protocol_specific_header.get() >> 8) & 0x3FU);
    }

    /// Get tcode field (bits 7:4 of protocol_specific_header)
    [[nodiscard]] constexpr auto tcode() const noexcept -> uint8_t
    {
        return static_cast<uint8_t>((protocol_specific_header.get() >> 4) & 0x0FU);
    }

    /// Get sy field (bits 3:0 of protocol_specific_header)
    [[nodiscard]] constexpr auto sy() const noexcept -> uint8_t
    {
        return static_cast<uint8_t>(protocol_specific_header.get() & 0x0FU);
    }

    /// Set protocol specific header fields for IEC 61883/IIDC
    constexpr void set_protocol_specific(uint8_t tag_val, uint8_t channel_val, uint8_t tcode_val, uint8_t sy_val) noexcept
    {
        uint16_t const value = static_cast<uint16_t>(
            ((tag_val & 0x03U) << 14) | ((channel_val & 0x3FU) << 8) | ((tcode_val & 0x0FU) << 4) | (sy_val & 0x0FU));
        protocol_specific_header = value;
    }

    // ========== Initialization ==========

    /// Initialize for IEC 61883/IIDC stream with version 1 header
    constexpr void init_61883_iidc(StreamId const& sid) noexcept
    {
        subtype = AvtpSubtype::iec_61883_iidc;
        sv_version_flags = 0x90U;  // sv=1, version=1
        format_specific_data_0 = 0U;
        reserved_tu = 0U;
        set_stream_id(sid);
        sequence_num = 0U;
        avtp_timestamp = 0U;
        ptp_grandmaster_identity = ClockIdentity{};
        gateway_info = 0U;
        stream_data_length = 0U;
        set_protocol_specific(1U, 0U, 0x0AU, 0U);  // tag=1 (CIP), tcode=0xA (stream)
    }

    // ========== Validation ==========

    /// Check if this is a valid IEC 61883/IIDC version 1 stream header
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool
    {
        if (subtype != AvtpSubtype::iec_61883_iidc) {
            return false;
        }
        if (!sv()) {
            return false;
        }
        if (version() != 1) {
            return false;
        }
        return true;
    }

    auto operator<=>(AvtpStreamHeaderV1 const& rhs) const noexcept -> std::strong_ordering = default;
};

// Compile-time layout verification
static_assert(sizeof(AvtpStreamHeaderV1) == 40, "AvtpStreamHeaderV1 must be exactly 40 bytes");
static_assert(alignof(AvtpStreamHeaderV1) <= 4, "AvtpStreamHeaderV1 alignment must not exceed 4 bytes");
static_assert(offsetof(AvtpStreamHeaderV1, subtype) == 0, "subtype must be at offset 0");
static_assert(offsetof(AvtpStreamHeaderV1, sv_version_flags) == 1, "sv_version_flags must be at offset 1");
static_assert(offsetof(AvtpStreamHeaderV1, format_specific_data_0) == 2, "format_specific_data_0 must be at offset 2");
static_assert(offsetof(AvtpStreamHeaderV1, reserved_tu) == 3, "reserved_tu must be at offset 3");
static_assert(offsetof(AvtpStreamHeaderV1, stream_id_) == 4, "stream_id must be at offset 4");
static_assert(offsetof(AvtpStreamHeaderV1, sequence_num) == 12, "sequence_num must be at offset 12");
static_assert(offsetof(AvtpStreamHeaderV1, avtp_timestamp) == 16, "avtp_timestamp must be at offset 16");
static_assert(offsetof(AvtpStreamHeaderV1, ptp_grandmaster_identity) == 24, "ptp_grandmaster_identity must be at offset 24");
static_assert(offsetof(AvtpStreamHeaderV1, gateway_info) == 32, "gateway_info must be at offset 32");
static_assert(offsetof(AvtpStreamHeaderV1, stream_data_length) == 36, "stream_data_length must be at offset 36");
static_assert(offsetof(AvtpStreamHeaderV1, protocol_specific_header) == 38, "protocol_specific_header must be at offset 38");

//
// Am824V1Pdu - Complete AM824 packet with version 1 stream header
//

/// AM824 V1 PDU - Combines AvtpStreamHeaderV1 + Cip61883Header
struct Am824V1Pdu
{
    /// Total length of AM824 V1 header on wire (stream header + CIP header)
    static constexpr size_t LENGTH = AvtpStreamHeaderV1::LENGTH + Cip61883Header::LENGTH;  // 48 bytes

    /// Minimum wire length (header only, no audio data)
    static constexpr size_t HEADER_LENGTH = LENGTH;

    /// Maximum channel count supported
    static constexpr size_t MAX_CHANNELS = 64;

    /// Maximum samples per packet
    static constexpr size_t MAX_SAMPLES_PER_PACKET = 32;

    /// Bytes per AM824 audio sample (label + 24-bit audio)
    static constexpr size_t BYTES_PER_SAMPLE = 4;

    // Header fields

    /// AVTP common stream header version 1
    AvtpStreamHeaderV1 stream_header{};

    /// IEC 61883 CIP header
    Cip61883Header cip_header{};

    // ========== Accessors ==========

    /// Get the stream ID
    [[nodiscard]] constexpr auto stream_id() const noexcept -> StreamId { return stream_header.stream_id(); }

    /// Set the stream ID
    constexpr void set_stream_id(StreamId const& sid) noexcept { stream_header.set_stream_id(sid); }

    /// Get the AVTP timestamp (64-bit)
    [[nodiscard]] constexpr auto avtp_timestamp() const noexcept -> uint64_t { return stream_header.get_avtp_timestamp(); }

    /// Set the AVTP timestamp (64-bit)
    constexpr void set_avtp_timestamp(uint64_t const value) noexcept { stream_header.set_avtp_timestamp(value); }

    /// Get the sequence number (32-bit)
    [[nodiscard]] constexpr auto get_sequence_num() const noexcept -> uint32_t { return stream_header.get_sequence_num(); }

    /// Set the sequence number (32-bit)
    constexpr void set_sequence_num(uint32_t const value) noexcept { stream_header.set_sequence_num(value); }

    /// Increment sequence number (wraps at 2^32)
    constexpr void increment_sequence_num() noexcept { stream_header.increment_sequence_num(); }

    /// Get the PTP grandmaster identity
    [[nodiscard]] constexpr auto get_ptp_grandmaster_identity() const noexcept -> ClockIdentity
    {
        return stream_header.get_ptp_grandmaster_identity();
    }

    /// Set the PTP grandmaster identity
    constexpr void set_ptp_grandmaster_identity(ClockIdentity const& gm) noexcept
    {
        stream_header.set_ptp_grandmaster_identity(gm);
    }

    /// Get channel count (from DBS field)
    [[nodiscard]] constexpr auto channel_count() const noexcept -> uint8_t { return cip_header.data_block_size(); }

    /// Set channel count
    constexpr void set_channel_count(uint8_t const count) noexcept { cip_header.set_data_block_size(count); }

    /// Get sample rate
    [[nodiscard]] constexpr auto sample_rate() const noexcept -> Am824SampleRate { return cip_header.sample_rate(); }

    /// Set sample rate
    constexpr void set_sample_rate(Am824SampleRate const rate) noexcept { cip_header.set_sample_rate(rate); }

    /// Get data block count
    [[nodiscard]] constexpr auto data_block_count() const noexcept -> uint8_t { return cip_header.data_block_count(); }

    /// Set data block count
    constexpr void set_data_block_count(uint8_t const value) noexcept { cip_header.set_data_block_count(value); }

    /// Increment data block count by sample count
    constexpr void increment_data_block_count(uint8_t const sample_count) noexcept
    {
        uint8_t const current = cip_header.data_block_count();
        cip_header.set_data_block_count(static_cast<uint8_t>((current + sample_count) & 0xFFU));
    }

    /// Get SYT timestamp
    [[nodiscard]] constexpr auto syt_timestamp() const noexcept -> uint16_t { return cip_header.syt_timestamp(); }

    /// Set SYT timestamp
    constexpr void set_syt_timestamp(uint16_t const value) noexcept { cip_header.set_syt_timestamp(value); }

    /// Check if timestamp is valid
    [[nodiscard]] constexpr auto tv() const noexcept -> bool { return stream_header.tv(); }

    /// Set timestamp valid
    constexpr void set_tv(bool const value) noexcept { stream_header.set_tv(value); }

    /// Get stream_data_length (includes CIP headers + audio payload)
    [[nodiscard]] constexpr auto stream_data_length() const noexcept -> uint16_t { return stream_header.get_stream_data_length(); }

    /// Set stream_data_length
    constexpr void set_stream_data_length(uint16_t const value) noexcept { stream_header.set_stream_data_length(value); }

    /// Calculate audio payload length from stream_data_length
    [[nodiscard]] constexpr auto audio_payload_length() const noexcept -> uint16_t
    {
        uint16_t const sdl = stream_data_length();
        return (sdl > Cip61883Header::LENGTH) ? static_cast<uint16_t>(sdl - Cip61883Header::LENGTH) : 0U;
    }

    /// Calculate sample count from stream_data_length and channel count
    [[nodiscard]] constexpr auto sample_count() const noexcept -> uint8_t
    {
        uint16_t const payload = audio_payload_length();
        uint8_t const channels = channel_count();
        if (channels == 0) {
            return 0U;
        }
        return static_cast<uint8_t>(payload / (channels * BYTES_PER_SAMPLE));
    }

    /// Set dimensions (sample count and channel count) and update stream_data_length
    constexpr void set_dimensions(uint8_t const samples, uint8_t const channels) noexcept
    {
        set_channel_count(channels);
        uint16_t const audio_payload = static_cast<uint16_t>(static_cast<size_t>(samples) * channels * BYTES_PER_SAMPLE);
        set_stream_data_length(static_cast<uint16_t>(Cip61883Header::LENGTH + audio_payload));
    }

    // ========== Validation ==========

    /// Check if this is a valid AM824 version 1 packet
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool
    {
        // Check subtype
        if (stream_header.subtype != AvtpSubtype::iec_61883_iidc) {
            return false;
        }
        // Check sv bit
        if (!stream_header.sv()) {
            return false;
        }
        // Check version
        if (stream_header.version() != 1) {
            return false;
        }
        // Check FMT
        if (cip_header.fmt() != AM824_FMT) {
            return false;
        }
        // Check sample rate is valid
        if (static_cast<uint8_t>(cip_header.format_dependent_field()) > 0x06U) {
            return false;
        }
        // Check channel count
        if (channel_count() == 0) {
            return false;
        }
        return true;
    }

    // ========== Initialization ==========

    /// Initialize for AM824 audio stream with version 1 header
    constexpr void init(StreamId const& sid, uint8_t const channels, Am824SampleRate const rate) noexcept
    {
        stream_header.init_61883_iidc(sid);
        cip_header.init_am824(channels, rate);
    }

    auto operator<=>(Am824V1Pdu const& rhs) const noexcept -> std::strong_ordering = default;
};

// Compile-time layout verification
static_assert(sizeof(Am824V1Pdu) == 48, "Am824V1Pdu must be exactly 48 bytes");
static_assert(alignof(Am824V1Pdu) <= 4, "Am824V1Pdu alignment must not exceed 4 bytes");
static_assert(offsetof(Am824V1Pdu, stream_header) == 0, "stream_header must be at offset 0");
static_assert(offsetof(Am824V1Pdu, cip_header) == 40, "cip_header must be at offset 40");

}  // namespace statusbar::avtp

// Serialization traits
template <>
struct statusbar::traits::is_serializable_wire_fixed_struct<statusbar::avtp::AvtpStreamHeaderV1> : std::true_type
{};

template <>
struct statusbar::traits::is_serializable_wire_fixed_struct<statusbar::avtp::Am824V1Pdu> : std::true_type
{};

namespace statusbar::avtp {

using protocol::load_unchecked;
using protocol::store_unchecked;

//
// Parse/create helpers
//

/// Parse an AM824 V1 packet header
/// @param packet Raw packet data including the AM824 V1 header
[[nodiscard]] auto am824_v1_parse_header(std::span<uint8_t const> packet) noexcept -> std::optional<Am824V1Pdu>;

/// Get audio payload span from an AM824 V1 packet
/// @param packet Raw packet data including the AM824 V1 header
[[nodiscard]] auto am824_v1_get_audio_payload(std::span<uint8_t const> packet) noexcept -> std::span<uint8_t const>;

}  // namespace statusbar::avtp
