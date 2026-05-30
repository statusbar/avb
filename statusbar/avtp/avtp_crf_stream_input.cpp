// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_crf_stream_input.hpp"

namespace statusbar::avtp {

CrfStreamInputContext::CrfStreamInputContext(CrfType t, uint32_t base_freq, CrfPull p, uint16_t ts_interval)
    : type{t}
    , base_frequency{base_freq}
    , pull{p}
    , timestamp_interval{ts_interval}
{}

void CrfStreamInputContext::reset() noexcept
{
    last_sequence_num = 0;
    last_timestamp_ns = 0;
    first_timestamp_ns = 0;
    last_timestamp_count = 0;
    has_valid_data = false;
    media_clock_restarted = false;
    packets_received = 0;
    sequence_gaps = 0;
    total_timestamps_received = 0;
}

void CrfStreamInputContext::update_sequence_num(uint8_t seq_num) noexcept
{
    if (packets_received > 0) {
        uint8_t const expected = static_cast<uint8_t>((last_sequence_num + 1U) & 0xFFU);
        if (seq_num != expected) {
            ++sequence_gaps;
        }
    }
    last_sequence_num = seq_num;
}

auto CrfStreamInputContext::nominal_period_ns() const noexcept -> double
{
    double const freq = crf_calculate_frequency(base_frequency, pull);
    if (freq <= 0.0) {
        return 0.0;
    }
    return 1'000'000'000.0 / freq;
}

}  // namespace statusbar::avtp
