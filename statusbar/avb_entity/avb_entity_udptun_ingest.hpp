#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// @file avb_entity_udptun_ingest.hpp
/// @brief Pure helpers for the local-stream -> inter-site tunnel ingest path.
///
/// Companion to avb_entity_udptun_egress.hpp in the EntityUdptunBridge
/// extraction (god-object phase 2): the buffer-agnostic, unit-testable byte
/// conversions the ingest path uses, kept out of AvbEntityAudioIO.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace statusbar::avb_entity {

/// Transcode AM824 MBLA data blocks to AAF-style int32 PCM into `out`.
///
/// Each MBLA quadlet is [label:8][audio:24] big-endian. The inter-site tunnel
/// and the far egress are AAF int32, so we drop the label and MSB-align the
/// 24-bit audio into an int32 ([a23:16][a15:8][a7:0][0x00] = audio << 8) — which
/// is exactly what stops the far end from reading the 0x40 label as the sample's
/// MSB. Lossless for 24-bit audio.
///
/// Processes floor(min(mbla, out)/4) quadlets and returns the bytes written.
inline auto udptun_am824_mbla_to_int32(std::span<uint8_t const> mbla, std::span<uint8_t> out) -> size_t
{
    size_t const quads = std::min(mbla.size(), out.size()) / 4;
    for (size_t q = 0; q < quads; ++q) {
        size_t const i = q * 4;
        out[i + 0] = mbla[i + 1];  // audio[23:16]
        out[i + 1] = mbla[i + 2];  // audio[15:8]
        out[i + 2] = mbla[i + 3];  // audio[7:0]
        out[i + 3] = 0x00;         // int32 low byte (24-bit MSB-aligned)
    }
    return quads * 4;
}

}  // namespace statusbar::avb_entity
