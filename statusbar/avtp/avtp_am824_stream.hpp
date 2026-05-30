#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// AM824 stream common — shared config, timestamp reconstruction, and label helpers
///
/// Provides Am824StreamConfig for per-stream configuration shared between input
/// and output contexts, plus utility functions for AM824 label extraction and
/// AVTP 32→64-bit timestamp reconstruction.
///
/// References:
///   IEEE Std 1722-2016 Section 5.4 (IEC 61883 encapsulation over AVTP)
///   IEC 61883-6:2005 Section 8.2 (AM824 label allocation)
///   TA Document 1999024 (SMPTE time code in AM824)

#include "statusbar/avtp/avtp_am824.hpp"
#include "statusbar/avtp/avtp_stream_common.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace statusbar::avtp {

/// Extract valid MIDI bytes from an AM824 MIDI-labeled quadlet (0x80-0x83)
/// @param quadlet The 32-bit AM824 quadlet
/// @param out Destination for extracted bytes (must have room for 3)
/// @return Number of valid bytes extracted (0-3)
[[nodiscard]] auto am824_extract_midi_bytes(uint32_t quadlet, uint8_t* out) noexcept -> uint8_t;

/// Extract SMPTE time code part from an AM824 SMPTE-labeled quadlet (0x88-0x8B)
/// @param quadlet The 32-bit AM824 quadlet
/// @param out Destination for the 3-byte payload
/// @return Part number (1=first, 2=middle, 3=last), or 0 if no data
[[nodiscard]] auto am824_extract_smpte_part(uint32_t quadlet, std::array<uint8_t, 3>& out) noexcept -> uint8_t;

/// Create an AM824 MIDI quadlet from 0-3 bytes
/// @param byte_count Number of valid MIDI bytes (0-3)
/// @param bytes Pointer to the MIDI bytes
/// @return Complete 32-bit AM824 quadlet with MIDI label (0x80+count)
[[nodiscard]] auto am824_create_midi_quadlet(uint8_t byte_count, uint8_t const* bytes) noexcept -> uint32_t;

/// Create an AM824 SMPTE quadlet from a part number and 3-byte payload
/// @param part Part number (1=first, 2=middle, 3=last)
/// @param payload The 3-byte SMPTE payload
/// @return Complete 32-bit AM824 quadlet with SMPTE label (0x88+part)
[[nodiscard]] auto am824_create_smpte_quadlet(uint8_t part, std::array<uint8_t, 3> const& payload) noexcept -> uint32_t;

/// Per-channel type assignment for mixed AM824 streams
enum class Am824ChannelType : uint8_t
{
    mbla,   ///< Multi-bit Linear Audio (label 0x40)
    midi,   ///< MIDI conformant (labels 0x80-0x83)
    smpte,  ///< SMPTE time code (labels 0x88-0x8B)
};

/// Shared stream configuration — common to input and output contexts.
/// Computed from sample rate and channel count at construction.
struct Am824StreamConfig
{
    Am824SampleRate sample_rate;
    uint8_t channel_count;
    uint8_t syt_interval;       ///< Samples between timestamp anchors
    uint64_t sample_period_ns;  ///< Duration of one sample in nanoseconds

    Am824StreamConfig(Am824SampleRate rate, uint8_t channels);
};

}  // namespace statusbar::avtp
