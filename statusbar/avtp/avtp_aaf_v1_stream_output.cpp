// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_aaf_v1_stream_output.hpp"

namespace statusbar::avtp {

AafV1StreamOutputContext::AafV1StreamOutputContext(
    StreamId sid, AafFormat fmt, AafSampleRate rate, uint16_t channels, uint8_t depth, uint64_t pres_offset, ClockIdentity gm)
    : format{fmt}
    , sample_rate{rate}
    , channel_count{channels}
    , bit_depth{depth}
    , sample_period_ns{aaf_sample_rate_hz(rate) > 0 ? 1'000'000'000ULL / aaf_sample_rate_hz(rate) : 0}
    , stream_id{sid}
    , presentation_offset_ns{pres_offset}
    , grandmaster_identity{gm}
{}

void AafV1StreamOutputContext::reset() noexcept
{
    sequence_num = 0;
    running_sample_count = 0;
    packets_sent = 0;
}

void AafV1StreamOutputContext::build_packet_header(AafV1Pdu& pdu, uint16_t samples_per_channel, uint64_t gptp_now_ns) noexcept
{
    pdu.init(stream_id, format, sample_rate, channel_count, bit_depth);
    pdu.set_sequence_num(sequence_num);
    pdu.set_dimensions(samples_per_channel, channel_count);

    // V1: full 64-bit presentation time — no truncation
    uint64_t const pts = gptp_now_ns + presentation_offset_ns;
    pdu.set_tv(true);
    pdu.set_sp(false);
    pdu.set_avtp_timestamp(pts);
    pdu.set_ptp_grandmaster_identity(grandmaster_identity);

    // Advance state
    running_sample_count += samples_per_channel;
    ++sequence_num;  // 32-bit, wraps naturally at 2^32
    ++packets_sent;
}

}  // namespace statusbar::avtp
