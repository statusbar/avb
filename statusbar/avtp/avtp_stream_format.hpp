#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// AVTP stream format code decoder.
///
/// The IEEE 1722-2016 stream format is an 8-byte value (carried as a u64 or
/// Eui64 on the wire, e.g. in DescriptorStream::current_format). Byte 0 is
/// the AVTP subtype; subsequent bytes are subtype-specific.
///
/// This header provides a decoder that produces a short human-readable
/// description like "AAF 2ch 48kHz 24-bit" or "AM824 MBLA 8ch 48kHz" for
/// common formats. Unknown formats fall back to a hex string.

#include "statusbar/avtp/avtp_types.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <string>

namespace statusbar::avtp {

/// Convert a u64 stream format code to an 8-byte array (big-endian on wire).
[[nodiscard]] constexpr auto stream_format_bytes(uint64_t fmt) noexcept -> std::array<uint8_t, 8>
{
    return {
        static_cast<uint8_t>((fmt >> 56) & 0xFFU),
        static_cast<uint8_t>((fmt >> 48) & 0xFFU),
        static_cast<uint8_t>((fmt >> 40) & 0xFFU),
        static_cast<uint8_t>((fmt >> 32) & 0xFFU),
        static_cast<uint8_t>((fmt >> 24) & 0xFFU),
        static_cast<uint8_t>((fmt >> 16) & 0xFFU),
        static_cast<uint8_t>((fmt >> 8) & 0xFFU),
        static_cast<uint8_t>(fmt & 0xFFU),
    };
}

/// AAF nsr (nominal sample rate) codes → Hz (IEEE 1722-2016 Table 11)
[[nodiscard]] constexpr auto aaf_nsr_to_hz(uint8_t nsr) noexcept -> uint32_t
{
    switch (nsr) {
        case 0x01:
            return 8000;
        case 0x02:
            return 16000;
        case 0x03:
            return 32000;
        case 0x04:
            return 44100;
        case 0x05:
            return 48000;
        case 0x06:
            return 88200;
        case 0x07:
            return 96000;
        case 0x08:
            return 176400;
        case 0x09:
            return 192000;
        case 0x0A:
            return 24000;
        default:
            return 0;
    }
}

/// AM824/IEC 61883-6 SFC (sampling frequency code) → Hz
[[nodiscard]] constexpr auto am824_sfc_to_hz(uint8_t sfc) noexcept -> uint32_t
{
    switch (sfc) {
        case 0:
            return 32000;
        case 1:
            return 44100;
        case 2:
            return 48000;
        case 3:
            return 88200;
        case 4:
            return 96000;
        case 5:
            return 176400;
        case 6:
            return 192000;
        default:
            return 0;
    }
}

/// Decode an AAF stream format. Layout (IEEE 1722-2016 Clause 7.3.4):
///   Byte 0: subtype (0x02)
///   Byte 1: nsr[7:4] | reserved[3:2] | channels_per_frame[9:8]
///   Byte 2: channels_per_frame[7:0]
///   Byte 3: bit_depth
///   Bytes 4-7: reserved
[[nodiscard]] inline auto decode_aaf_stream_format(uint64_t fmt) -> std::string
{
    auto const b = stream_format_bytes(fmt);
    uint8_t const nsr = (b[1] >> 4) & 0x0FU;
    uint16_t const channels = (static_cast<uint16_t>(b[1] & 0x03U) << 8) | b[2];
    uint8_t const depth = b[3];
    uint32_t const rate = aaf_nsr_to_hz(nsr);
    if (rate == 0) {
        return std::format("AAF rate?({}) {}ch {}-bit", nsr, channels, depth);
    }
    if (rate % 1000 == 0) {
        return std::format("AAF {}ch {}kHz {}-bit", channels, rate / 1000, depth);
    }
    return std::format("AAF {}ch {}.{}kHz {}-bit", channels, rate / 1000, (rate / 100) % 10, depth);
}

/// Decode an AM824/IEC 61883-6 stream format.
/// Layout per IEEE 1722.1 Annex A.5:
///   Byte 0: subtype (0x00 iec_61883_iidc)
///   Byte 1: sf[7] | fmt[6:1] | other — sf=1 + fmt=0x10 indicates IEC 61883-6 (AM824)
///   Byte 2: SFC (sampling frequency code, whole byte) — e.g. 0x02 = 48 kHz
///   Byte 3: DBS (data block size = channel count)
///   Byte 4: b[7] | nb[6] | reserved[5:2] | sph[1] | reserved[0]
///   Byte 5: label
///   Bytes 6-7: reserved
///
/// Example from the audio interface: 0x00a0020840000800
///   byte 2 = 0x02 → SFC 48 kHz
///   byte 3 = 0x08 → 8-channel AM824
[[nodiscard]] inline auto decode_am824_stream_format(uint64_t fmt) -> std::string
{
    auto const b = stream_format_bytes(fmt);
    uint8_t const sfc = b[2];
    uint8_t const channels = b[3];
    uint32_t const rate = am824_sfc_to_hz(sfc);
    if (rate == 0) {
        return std::format("AM824 {}ch SFC=0x{:02x}", channels, sfc);
    }
    if (rate % 1000 == 0) {
        return std::format("AM824 {}ch {}kHz", channels, rate / 1000);
    }
    return std::format("AM824 {}ch {}.{}kHz", channels, rate / 1000, (rate / 100) % 10);
}

/// Decode an 8-byte stream format code into a short human-readable string.
/// Recognises AAF and AM824 (IEC 61883-6). Falls back to hex for others.
[[nodiscard]] inline auto stream_format_to_string(uint64_t fmt) -> std::string
{
    auto const b = stream_format_bytes(fmt);
    switch (b[0]) {
        case AvtpSubtype::aaf:
            return decode_aaf_stream_format(fmt);
        case AvtpSubtype::iec_61883_iidc:
            return decode_am824_stream_format(fmt);
        case AvtpSubtype::crf:
            return "CRF";
        default:
            return std::format("0x{:016x}", fmt);
    }
}

}  // namespace statusbar::avtp
