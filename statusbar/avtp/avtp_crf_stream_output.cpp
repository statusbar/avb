// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_crf_stream_output.hpp"

#include <cmath>

namespace statusbar::avtp {

CrfStreamOutputContext::CrfStreamOutputContext(
    StreamId sid, CrfType t, uint32_t base_freq, CrfPull p, uint16_t ts_interval, uint16_t ts_per_pkt)
    : type{t}
    , base_frequency{base_freq}
    , pull{p}
    , timestamp_interval{ts_interval}
    , timestamps_per_packet{ts_per_pkt}
    , stream_id{sid}
{}

void CrfStreamOutputContext::reset() noexcept
{
    sequence_num = 0;
    next_timestamp_ns = 0;
    started = false;
    packets_sent = 0;
    total_timestamps_sent = 0;
}

auto CrfStreamOutputContext::nominal_period_ns() const noexcept -> double
{
    double const freq = crf_calculate_frequency(base_frequency, pull);
    if (freq <= 0.0) {
        return 0.0;
    }
    return 1'000'000'000.0 / freq;
}

auto CrfStreamOutputContext::timestamp_spacing_ns() const noexcept -> double
{
    return nominal_period_ns() * timestamp_interval;
}

auto CrfStreamOutputContext::build_packet(CrfPdu& pdu, std::span<uint8_t> timestamp_data, uint64_t gptp_now_ns) noexcept -> size_t
{
    size_t const required = static_cast<size_t>(timestamps_per_packet) * CrfPdu::TIMESTAMP_SIZE;
    if (timestamp_data.size() < required || timestamps_per_packet == 0) {
        return 0;
    }

    // Initialize header
    switch (type) {
        case CrfType::audio_sample:
            pdu.init_audio_sample(stream_id, base_frequency, pull, timestamp_interval, timestamps_per_packet);
            break;
        case CrfType::video_frame:
            pdu.init_video_frame(stream_id, base_frequency, pull, timestamp_interval, timestamps_per_packet);
            break;
        case CrfType::video_line:
            pdu.init_video_line(stream_id, base_frequency, pull, timestamp_interval, timestamps_per_packet);
            break;
        case CrfType::machine_cycle:
            pdu.init_machine_cycle(stream_id, base_frequency, pull, timestamp_interval, timestamps_per_packet);
            break;
        default:
            pdu.init_audio_sample(stream_id, base_frequency, pull, timestamp_interval, timestamps_per_packet);
            break;
    }
    pdu.set_sequence_num(sequence_num);

    // Anchor timestamp sequence on first call
    if (!started) {
        next_timestamp_ns = gptp_now_ns;
        started = true;
    }

    // Compute spacing between consecutive timestamps
    double const spacing = timestamp_spacing_ns();
    uint64_t const spacing_int = static_cast<uint64_t>(std::round(spacing));

    // Write timestamps
    for (uint16_t i = 0; i < timestamps_per_packet; ++i) {
        (void)crf_set_timestamp(timestamp_data, i, next_timestamp_ns);
        next_timestamp_ns += spacing_int;
    }

    // Advance state
    sequence_num = static_cast<uint8_t>((sequence_num + 1U) & 0xFFU);
    total_timestamps_sent += timestamps_per_packet;
    ++packets_sent;

    return required;
}

}  // namespace statusbar::avtp
