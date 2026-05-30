// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_am824_stream.hpp"

namespace statusbar::avtp {

auto am824_extract_midi_bytes(uint32_t quadlet, uint8_t* out) noexcept -> uint8_t
{
    uint8_t const label = static_cast<uint8_t>((quadlet >> 24) & 0xFFU);
    uint8_t const counter = label & 0x03U;
    for (uint8_t b = 0; b < counter; ++b) {
        out[b] = static_cast<uint8_t>((quadlet >> (16 - (b * 8))) & 0xFFU);
    }
    return counter;
}

auto am824_extract_smpte_part(uint32_t quadlet, std::array<uint8_t, 3>& out) noexcept -> uint8_t
{
    uint8_t const label = static_cast<uint8_t>((quadlet >> 24) & 0xFFU);
    uint8_t const counter = label & 0x03U;
    if (counter == 0) {
        return 0;
    }
    out[0] = static_cast<uint8_t>((quadlet >> 16) & 0xFFU);
    out[1] = static_cast<uint8_t>((quadlet >> 8) & 0xFFU);
    out[2] = static_cast<uint8_t>(quadlet & 0xFFU);
    return counter;
}

auto am824_create_midi_quadlet(uint8_t byte_count, uint8_t const* bytes) noexcept -> uint32_t
{
    uint8_t const label = static_cast<uint8_t>(AM824_LABEL_RAW_MIDI + (byte_count & 0x03U));
    uint32_t q = static_cast<uint32_t>(label) << 24;
    for (uint8_t b = 0; b < byte_count && b < 3; ++b) {
        q |= static_cast<uint32_t>(bytes[b]) << (16 - (b * 8));
    }
    return q;
}

auto am824_create_smpte_quadlet(uint8_t part, std::array<uint8_t, 3> const& payload) noexcept -> uint32_t
{
    uint8_t const label = static_cast<uint8_t>(AM824_LABEL_SMPTE + (part & 0x03U));
    return (static_cast<uint32_t>(label) << 24) | (static_cast<uint32_t>(payload[0]) << 16) |
        (static_cast<uint32_t>(payload[1]) << 8) | static_cast<uint32_t>(payload[2]);
}

Am824StreamConfig::Am824StreamConfig(Am824SampleRate rate, uint8_t channels)
    : sample_rate{rate}
    , channel_count{channels}
    , syt_interval{am824_syt_interval(rate)}
    , sample_period_ns{am824_sample_rate_hz(rate) > 0 ? 1'000'000'000ULL / am824_sample_rate_hz(rate) : 0}
{}

}  // namespace statusbar::avtp
