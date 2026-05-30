// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/gptp/gptp_servo.hpp"

#include "statusbar/gptp/gptp_time_util.hpp"

#include <algorithm>
#include <cmath>

namespace statusbar::gptp {

auto ServoLoop::suppress_for_negative_time_jump(ServoOutput& out, int64_t corrected_master_ns, int64_t sync_rx_local_ns) noexcept
    -> ServoOutput
{
    out.servo_suppressed = true;
    out.rate_ratio = 1.0;
    previous_master_ns_ = corrected_master_ns;
    previous_local_ns_ = sync_rx_local_ns;
    return out;
}

auto ServoLoop::apply_phase_jump(ServoOutput& out, int64_t phase_error_ns) noexcept -> ServoOutput
{
    out.phase_jump_ns = phase_error_ns;
    current_ppm_ = 0.0;
    consecutive_phase_jumps_ = 0;
    have_previous_ = false;
    out.frequency_adjust_ppb = 0.0;
    return out;
}

auto ServoLoop::process(MDSyncReceiveIndication const& ind, int64_t mean_link_delay_ns, double neighbor_rate_ratio) noexcept
    -> ServoOutput
{
    ServoOutput out{};

    // Convert the FollowUp's preciseOriginTimestamp to a single
    // nanosecond value on the grandmaster's timebase.
    int64_t const precise_origin_ns = ts_to_ns(ind.precise_origin_timestamp);

    // Extract the scaled rate offset from the FollowUp Information
    // TLV, if present. The TLV value is scaled by 2^-41 per
    // 802.1AS-2020 Clause 11.4.4.3. The result is the accumulated
    // (grandmaster-to-upstream-neighbor) rate ratio, expressed as
    // an offset from 1.0.
    //
    // We apply this to the meanLinkDelay to convert the peer
    // contribution to our timebase, then divide by neighborRateRatio
    // to account for our direct peer's clock offset from us.
    //
    // Ported from OpenAvnu ptp_message.cpp:1046-1049.
    double master_local_freq_offset = 1.0;
    if (ind.has_follow_up_tlv) {
        // Convert the int32 scaled rate offset to a double. The
        // cumulative scaled rate offset is (ratio - 1.0) * 2^41.
        double const raw = static_cast<double>(ind.cumulative_scaled_rate_offset);
        master_local_freq_offset = 1.0 + (raw / static_cast<double>(1LL << 41));
    }
    if (neighbor_rate_ratio > 0.0) {
        master_local_freq_offset /= neighbor_rate_ratio;
    }

    // Total correction: add the peer-side link delay adjusted for
    // rate offset, plus the correctionField from Sync+FollowUp.
    double const link_delay_in_master_time = static_cast<double>(mean_link_delay_ns) * master_local_freq_offset;
    int64_t const corrected_master_ns =
        precise_origin_ns + static_cast<int64_t>(std::llround(link_delay_in_master_time)) + ind.correction_field_ns;

    // Phase offset (master - local). Positive means we are behind
    // the master; a positive phase_jump_ns advances our clock to
    // catch up.
    int64_t const phase_error_ns = corrected_master_ns - ind.sync_rx_local_ns;
    out.master_offset_ns = phase_error_ns;
    out.corrected_master_ns = corrected_master_ns;

    // Rate ratio from two consecutive sync samples (for the PI
    // "rate difference" term). Requires a previous sample; on the
    // first call we skip the rate term and use 1.0.
    double rate_diff = 1.0;
    if (have_previous_) {
        int64_t const master_delta = corrected_master_ns - previous_master_ns_;
        int64_t const local_delta = ind.sync_rx_local_ns - previous_local_ns_;

        // Negative time jump: grandmaster went backwards between two
        // consecutive syncs. OpenAvnu ieee1588clock.cpp:336-342 maps
        // this to NEGATIVE_TIME_JUMP (0.0) which poisons the
        // controller — we interpret that as "suppress this sample".
        if (master_delta < 0) {
            return suppress_for_negative_time_jump(out, corrected_master_ns, ind.sync_rx_local_ns);
        }

        if (local_delta > 0) {
            rate_diff = static_cast<double>(master_delta) / static_cast<double>(local_delta);
        }
    }
    out.rate_ratio = rate_diff;
    previous_master_ns_ = corrected_master_ns;
    previous_local_ns_ = ind.sync_rx_local_ns;
    have_previous_ = true;

    // Phase jump detection — OpenAvnu ieee1588clock.cpp:418-422.
    // If the absolute phase error exceeds the jump threshold for
    // N consecutive samples, issue a step and reset the integrator.
    //
    // Crucially, while waiting for the jump-confirmation count to
    // mature, we MUST NOT pump the PI integrator with the (huge)
    // phase_error_ns — doing so winds the integral straight to the
    // ppm rail in the wrong direction and the servo can never
    // recover. The original code fell through to the PI block; this
    // returns early so the integrator is suppressed during any
    // out-of-band sample.
    int64_t const abs_error = phase_error_ns < 0 ? -phase_error_ns : phase_error_ns;
    if (abs_error > config_.servo_phase_jump_threshold_ns) {
        if (consecutive_phase_jumps_ < UINT8_MAX) {
            ++consecutive_phase_jumps_;
        }
        if (consecutive_phase_jumps_ >= config_.servo_phase_jump_consecutive_samples) {
            return apply_phase_jump(out, phase_error_ns);
        }
        // Not enough consecutive large errors yet to step. Don't
        // pump the PI controller with garbage — leave current_ppm_
        // alone and emit no frequency adjustment this cycle.
        out.servo_suppressed = true;
        return out;
    }
    consecutive_phase_jumps_ = 0;

    // PI controller update (OpenAvnu ieee1588clock.cpp:423-434).
    // sync_per_sec = 2^(-log_sync_interval); for log_sync = -3 this
    // is 8 syncs/s, for log_sync = -5 it's 32 syncs/s.
    double const sync_per_sec = std::pow(2.0, -static_cast<double>(ind.log_message_interval));

    // Integral term: integrates phase error over time, weighted by
    // the configured integral gain and normalized by the sync rate.
    double const integral_delta = config_.servo_integral_gain * sync_per_sec * static_cast<double>(phase_error_ns);

    // Proportional term: responds directly to the instantaneous
    // rate difference from 1.0, scaled to ppm (× 1e6).
    double const proportional_delta = config_.servo_proportional_gain * (rate_diff - 1.0) * 1e6;

    current_ppm_ += integral_delta + proportional_delta;
    current_ppm_ = std::clamp(current_ppm_, -config_.servo_ppm_limit, config_.servo_ppm_limit);

    // Convert ppm -> ppb at the interface boundary.
    out.frequency_adjust_ppb = current_ppm_ * 1000.0;
    return out;
}

}  // namespace statusbar::gptp
