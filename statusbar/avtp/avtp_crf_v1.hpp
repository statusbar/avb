#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// CRF Version 1 - Clock Reference Format with version 1 alternative header
/// IEEE 1722-2025 Clause 10 with version 1 header per 4.7.6
///
/// Version 1 improvements over version 0:
///   - sequence_num: 32-bit (was 8-bit via sequence_num_lsb only)
///   - ptp_grandmaster_identity: 8 bytes - clock domain identification
///
/// Wire format version 1 (36-byte header + variable crf_data), per Figure 99:
///   Byte 0:      subtype (0x04)
///   Byte 1:      sv[7] | version=1[6:4] | reserved[3:0]
///   Bytes 2-3:   reserved (continuation of 20-bit reserved field)
///   Bytes 4-7:   sequence_num (32-bit)
///   Bytes 8-15:  gptp_grandmaster_identity (8 bytes)
///   Byte 16:     reserved[7:4] | mr[3] | r[2] | fs[1] | tu[0]
///   Byte 17:     sequence_num_lsb (low 8 bits copy of sequence_num)
///   Byte 18:     type (CRF type: audio_sample, video_frame, etc.)
///   Byte 19:     reserved
///   Bytes 20-27: stream_id (8 bytes)
///   Bytes 28-31: pull[31:29] | base_frequency[28:0]
///   Bytes 32-33: crf_data_length
///   Bytes 34-35: timestamp_interval
///   Bytes 36+:   crf_data (64-bit timestamps)

#include "statusbar/avtp/avtp_crf.hpp"
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
// CrfV1Pdu - IEEE 1722-2025 Clause 10, Figure 99 (version 1)
// Based on AVTPDU alternative header version 1 (Figure 14)
//

/// CRF Protocol Data Unit Header version 1 - IEEE 1722-2025 Clause 10 (subtype 0x04, version 1)
struct CrfV1Pdu
{
    /// Total length of CRF V1 header on wire (header only, timestamps are variable)
    static constexpr size_t LENGTH = 36;
    static constexpr size_t HEADER_LENGTH = LENGTH;

    /// Size of each CRF timestamp in bytes
    static constexpr size_t TIMESTAMP_SIZE = 8;

    // ---- Alternative header common fields (bytes 0-15) ----

    /// Byte 0: subtype[7:0] = 0x04 for CRF
    octet_t subtype;

    /// Byte 1: sv[7] | version[6:4] | reserved[3:0]
    octet_t sv_version_rsv;

    /// Bytes 2-3: reserved (continuation of 20-bit reserved from byte 1)
    doublet_t reserved1;

    /// Bytes 4-7: sequence_num (32-bit)
    quadlet_t sequence_num;

    /// Bytes 8-15: gptp_grandmaster_identity
    ClockIdentity ptp_grandmaster_identity;

    // ---- CRF format-specific fields (alternative_data_payload, bytes 16+) ----

    /// Byte 16: reserved[7:4] | mr[3] | r[2] | fs[1] | tu[0]
    octet_t rsv_mr_r_fs_tu;

    /// Byte 17: sequence_num_lsb (low 8 bits copy of sequence_num, per 10.4.6)
    octet_t sequence_num_lsb;

    /// Byte 18: type (CRF type: audio_sample, video_frame, etc.)
    octet_t type;

    /// Byte 19: reserved
    octet_t reserved2;

    /// Bytes 20-27: stream_id
    StreamId stream_id_;

    /// Bytes 28-31: pull[31:29] | base_frequency[28:0]
    quadlet_t pull_base_frequency;

    /// Bytes 32-33: crf_data_length
    doublet_t crf_data_length_;

    /// Bytes 34-35: timestamp_interval
    doublet_t timestamp_interval_;

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

    /// Set the sequence number (also updates sequence_num_lsb per 10.4.6)
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

    // ========== Accessors - CRF format-specific fields ==========

    /// Get the media clock restart (mr) bit
    [[nodiscard]] constexpr auto mr() const noexcept -> bool { return rsv_mr_r_fs_tu.has_flag(0x08U); }

    /// Set the media clock restart (mr) bit
    constexpr void set_mr(bool const value) noexcept { rsv_mr_r_fs_tu.set_flag(0x08U, value); }

    /// Get the frame sync (fs) bit
    [[nodiscard]] constexpr auto fs() const noexcept -> bool { return rsv_mr_r_fs_tu.has_flag(0x02U); }

    /// Set the frame sync (fs) bit
    constexpr void set_fs(bool const value) noexcept { rsv_mr_r_fs_tu.set_flag(0x02U, value); }

    /// Get the timestamp uncertain (tu) bit
    [[nodiscard]] constexpr auto tu() const noexcept -> bool { return rsv_mr_r_fs_tu.has_flag(0x01U); }

    /// Set the timestamp uncertain (tu) bit
    constexpr void set_tu(bool const value) noexcept { rsv_mr_r_fs_tu.set_flag(0x01U, value); }

    /// Get the CRF type
    [[nodiscard]] constexpr auto get_type() const noexcept -> CrfType { return static_cast<CrfType>(type.get()); }

    /// Set the CRF type
    constexpr void set_type(CrfType const t) noexcept { type = static_cast<uint8_t>(t); }

    /// Get the stream ID
    [[nodiscard]] constexpr auto stream_id() const noexcept -> StreamId { return stream_id_; }

    /// Set the stream ID
    constexpr void set_stream_id(StreamId const& sid) noexcept { stream_id_ = sid; }

    /// Get the pull field (3 bits)
    [[nodiscard]] constexpr auto pull() const noexcept -> uint8_t
    {
        return static_cast<uint8_t>((pull_base_frequency.get() >> 29) & 0x07U);
    }

    /// Set the pull field (3 bits)
    constexpr void set_pull(uint8_t const value) noexcept
    {
        uint32_t const current = pull_base_frequency.get();
        pull_base_frequency = (current & 0x1FFFFFFFU) | (static_cast<uint32_t>(value & 0x07U) << 29);
    }

    /// Get the base_frequency field (29 bits)
    [[nodiscard]] constexpr auto base_frequency() const noexcept -> uint32_t { return pull_base_frequency.get() & 0x1FFFFFFFU; }

    /// Set the base_frequency field (29 bits)
    constexpr void set_base_frequency(uint32_t const freq) noexcept
    {
        uint32_t const current = pull_base_frequency.get();
        pull_base_frequency = (current & 0xE0000000U) | (freq & 0x1FFFFFFFU);
    }

    /// Get the crf_data_length
    [[nodiscard]] constexpr auto crf_data_length() const noexcept -> uint16_t { return crf_data_length_.get(); }

    /// Set the crf_data_length
    constexpr void set_crf_data_length(uint16_t const len) noexcept { crf_data_length_ = len; }

    /// Get timestamp count (crf_data_length / 8)
    [[nodiscard]] constexpr auto timestamp_count() const noexcept -> uint16_t
    {
        return static_cast<uint16_t>(crf_data_length() / TIMESTAMP_SIZE);
    }

    /// Get the timestamp_interval
    [[nodiscard]] constexpr auto timestamp_interval() const noexcept -> uint16_t { return timestamp_interval_.get(); }

    /// Set the timestamp_interval
    constexpr void set_timestamp_interval(uint16_t const interval) noexcept { timestamp_interval_ = interval; }

    /// Calculate actual frequency from base_frequency and pull
    [[nodiscard]] constexpr auto actual_frequency() const noexcept -> double
    {
        return crf_calculate_frequency(base_frequency(), pull());
    }

    // ========== Initialization ==========

    /// Initialize for CRF audio sample clock (most common use case)
    constexpr void init_audio_sample(
        StreamId const& sid, uint32_t const freq, CrfPull const pull_val, uint16_t const interval) noexcept
    {
        subtype = AvtpSubtype::crf;
        sv_version_rsv = 0x90U;  // sv=1, version=1
        reserved1 = 0U;
        sequence_num = 0U;
        ptp_grandmaster_identity = ClockIdentity{};
        rsv_mr_r_fs_tu = 0U;
        sequence_num_lsb = 0U;
        set_type(CrfType::audio_sample);
        reserved2 = 0U;
        stream_id_ = sid;
        pull_base_frequency = 0U;
        set_pull(static_cast<uint8_t>(pull_val));
        set_base_frequency(freq);
        crf_data_length_ = 0U;
        set_timestamp_interval(interval);
    }

    /// Generic initialization
    constexpr void init(
        StreamId const& sid, CrfType const crf_type, uint32_t const freq, CrfPull const pull_val, uint16_t const interval) noexcept
    {
        subtype = AvtpSubtype::crf;
        sv_version_rsv = 0x90U;  // sv=1, version=1
        reserved1 = 0U;
        sequence_num = 0U;
        ptp_grandmaster_identity = ClockIdentity{};
        rsv_mr_r_fs_tu = 0U;
        sequence_num_lsb = 0U;
        set_type(crf_type);
        reserved2 = 0U;
        stream_id_ = sid;
        pull_base_frequency = 0U;
        set_pull(static_cast<uint8_t>(pull_val));
        set_base_frequency(freq);
        crf_data_length_ = 0U;
        set_timestamp_interval(interval);
    }

    // ========== Validation ==========

    /// Check if this is a valid CRF version 1 header
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool
    {
        if (subtype != AvtpSubtype::crf) {
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

    auto operator<=>(CrfV1Pdu const& rhs) const noexcept -> std::strong_ordering = default;
};

// Compile-time layout verification
static_assert(sizeof(CrfV1Pdu) == 36, "CrfV1Pdu must be exactly 36 bytes");
static_assert(alignof(CrfV1Pdu) <= 4, "CrfV1Pdu alignment must not exceed 4 bytes");
static_assert(offsetof(CrfV1Pdu, subtype) == 0, "subtype must be at offset 0");
static_assert(offsetof(CrfV1Pdu, sv_version_rsv) == 1, "sv_version_rsv must be at offset 1");
static_assert(offsetof(CrfV1Pdu, reserved1) == 2, "reserved1 must be at offset 2");
static_assert(offsetof(CrfV1Pdu, sequence_num) == 4, "sequence_num must be at offset 4");
static_assert(offsetof(CrfV1Pdu, ptp_grandmaster_identity) == 8, "ptp_grandmaster_identity must be at offset 8");
static_assert(offsetof(CrfV1Pdu, rsv_mr_r_fs_tu) == 16, "rsv_mr_r_fs_tu must be at offset 16");
static_assert(offsetof(CrfV1Pdu, sequence_num_lsb) == 17, "sequence_num_lsb must be at offset 17");
static_assert(offsetof(CrfV1Pdu, type) == 18, "type must be at offset 18");
static_assert(offsetof(CrfV1Pdu, stream_id_) == 20, "stream_id must be at offset 20");
static_assert(offsetof(CrfV1Pdu, pull_base_frequency) == 28, "pull_base_frequency must be at offset 28");
static_assert(offsetof(CrfV1Pdu, crf_data_length_) == 32, "crf_data_length must be at offset 32");
static_assert(offsetof(CrfV1Pdu, timestamp_interval_) == 34, "timestamp_interval must be at offset 34");

}  // namespace statusbar::avtp

// Serialization trait
template <>
struct statusbar::traits::is_serializable_wire_fixed_struct<statusbar::avtp::CrfV1Pdu> : std::true_type
{};

namespace statusbar::avtp {

using protocol::load_unchecked;
using protocol::store_unchecked;

//
// Parse/create helpers
//

/// Parse a CRF V1 header from a packet
/// @param packet Raw packet data (must have subtype==CRF and version==1)
[[nodiscard]] auto crf_v1_parse_header(std::span<uint8_t const> packet) noexcept -> std::optional<CrfV1Pdu>;

/// Get CRF timestamp data span from a CRF V1 packet
/// @param packet Raw packet data including the CRF V1 header
[[nodiscard]] auto crf_v1_get_timestamp_data(std::span<uint8_t const> packet) noexcept -> std::span<uint8_t const>;

}  // namespace statusbar::avtp
