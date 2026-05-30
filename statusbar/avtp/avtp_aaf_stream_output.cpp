// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_aaf_stream_output.hpp"

namespace statusbar::avtp {

AafStreamOutputContext::AafStreamOutputContext(
    StreamId sid, AafFormat fmt, AafSampleRate rate, uint16_t channels, uint8_t depth, uint64_t pres_offset)
    : format{fmt}
    , sample_rate{rate}
    , channel_count{channels}
    , bit_depth{depth}
    , sample_period_ns{aaf_sample_rate_hz(rate) > 0 ? 1'000'000'000ULL / aaf_sample_rate_hz(rate) : 0}
    , stream_id{sid}
    , presentation_offset_ns{pres_offset}
{}

void AafStreamOutputContext::reset() noexcept
{
    sequence_num = 0;
    running_sample_count = 0;
    packets_sent = 0;
}

void AafStreamOutputContext::build_packet_header(AafPdu& pdu, uint16_t samples_per_channel, uint64_t gptp_now_ns) noexcept
{
    pdu.init(stream_id, format, sample_rate, channel_count, bit_depth);
    pdu.set_sequence_num(sequence_num);
    pdu.set_dimensions(samples_per_channel, channel_count);

    // AAF non-sparse: every packet gets a valid timestamp (sp=0, tv=1).
    // The presentation time is gptp_now + offset for the first sample.
    uint64_t const pts = gptp_now_ns + presentation_offset_ns;
    pdu.set_tv(true);
    pdu.set_sp(false);
    pdu.set_avtp_timestamp(static_cast<uint32_t>(pts & 0xFFFF'FFFFU));

    // Advance state
    running_sample_count += samples_per_channel;
    sequence_num = static_cast<uint8_t>((sequence_num + 1U) & 0xFFU);
    ++packets_sent;
}

}  // namespace statusbar::avtp
