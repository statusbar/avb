#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

//
// ClockSlaveControl / ServoLoop — discipline the local clock from
// MDSyncReceive indications and the current meanLinkDelay +
// neighborRateRatio supplied by MDPdelayReq (or by config in
// Automotive Profile with Pdelay disabled).
//
// Behavior is ported from OpenAvnu ieee1588clock.cpp:306-440:
//
//   - calcMasterLocalClockRateDifference() maps master time deltas
//     to local time deltas, producing a rate estimate.
//
//   - setMasterOffset() applies the correction: large phase errors
//     (over the configured jump threshold for N consecutive samples)
//     trigger a step adjustment; smaller errors feed the PI
//     controller which integrates into a frequency ppm value clamped
//     to ±servo_ppm_limit.
//
//   - Negative time jumps (master clock going backwards between two
//     successive syncs) suppress the servo for one cycle; a
//     negative-going grandmaster clock would otherwise poison the
//     integrator.
//
// The servo is stateful but is NOT an FSM — it's a straightforward
// controller with a small state vector. The participant feeds it
// one MDSyncReceive indication per sync-matched pair and then calls
// GptpClockOps to apply the resulting phase / frequency adjustments.
//

#include "statusbar/gptp/gptp_clock_ops.hpp"
#include "statusbar/gptp/gptp_config.hpp"
#include "statusbar/gptp/gptp_md_sync_receive.hpp"

#include <cstdint>
#include <optional>

namespace statusbar::gptp {

/// Servo adjustment produced by consuming one MDSyncReceive indication.
///
/// The participant applies these by calling the corresponding
/// GptpClockOps callbacks. If both phase_jump_ns and
/// frequency_adjust_ppb are set the phase jump is applied first and
/// the frequency adjustment second.
struct ServoOutput
{
    /// If set, the local clock should be stepped by this many ns
    /// (positive = advance). A phase jump resets the servo integrator.
    std::optional<int64_t> phase_jump_ns{};

    /// If set, the local clock frequency offset (in parts per billion;
    /// positive = speed up) relative to nominal.
    std::optional<double> frequency_adjust_ppb{};

    /// The master-to-local phase error for this sync, in ns.
    /// Positive means the local clock is behind master time.
    /// Reported for diagnostic callbacks regardless of whether a
    /// phase jump or frequency adjustment was issued.
    int64_t master_offset_ns{0};

    /// The ratio of the master (grandmaster) clock rate to the
    /// local clock rate, as measured across two consecutive syncs.
    /// 1.0 is a perfect match. Reported for diagnostic callbacks.
    double rate_ratio{1.0};

    /// True if the servo consumed this sync without issuing any
    /// correction (e.g. insufficient samples to compute rate,
    /// negative time jump suppression). The participant should
    /// still deliver on_sync_update observer callbacks since
    /// master_offset_ns and rate_ratio are valid.
    bool servo_suppressed{false};

    /// Master TAI time at the moment Sync arrived at the slave —
    /// preciseOriginTimestamp + correction_field + rate-corrected
    /// link_delay. Together with `sync_rx_local_ns` from the
    /// MDSyncReceiveIndication this forms a hardware-stamped anchor
    /// pair (master_TAI, local_PHC) that downstream consumers (e.g.
    /// the time bridge) can use as the basis for time-domain
    /// conversions instead of sampling get_local_time_ns at
    /// observer-callback time.
    int64_t corrected_master_ns{0};
};

/// PI servo controller ported from OpenAvnu ieee1588clock.cpp:350-440.
///
/// Consumes one MDSyncReceive indication per call to `process()`
/// and produces a ServoOutput describing the phase / frequency
/// correction to apply.
class ServoLoop
{
  public:
    explicit ServoLoop(GptpConfig const& config) noexcept
        : config_{config}
    {}

    /// Reset the servo state (called on port link down, sync loss,
    /// or large phase jump).
    void reset() noexcept
    {
        current_ppm_ = 0.0;
        consecutive_phase_jumps_ = 0;
        have_previous_ = false;
        previous_master_ns_ = 0;
        previous_local_ns_ = 0;
    }

    /// Consume one completed Sync+FollowUp pair plus the current best
    /// estimate of meanLinkDelay (from MDPdelayReq or from config
    /// when Pdelay is disabled). Returns the adjustment the
    /// participant should apply via GptpClockOps.
    [[nodiscard]] auto process(MDSyncReceiveIndication const& ind, int64_t mean_link_delay_ns, double neighbor_rate_ratio) noexcept
        -> ServoOutput;

    /// Current frequency offset in ppm (internal state for diagnostics).
    [[nodiscard]] auto current_ppm() const noexcept -> double { return current_ppm_; }

    /// Number of consecutive samples with phase error above the jump
    /// threshold — for diagnostics. Once this reaches
    /// config.servo_phase_jump_consecutive_samples, the next sample
    /// triggers a jump.
    [[nodiscard]] auto consecutive_phase_jumps() const noexcept -> uint8_t { return consecutive_phase_jumps_; }

  private:
    GptpConfig config_;

    /// Suppress servo for one cycle on negative time jump
    /// (grandmaster went backward). Updates the previous-sample
    /// tracking so the next cycle can compute a clean rate.
    auto suppress_for_negative_time_jump(ServoOutput& out, int64_t corrected_master_ns, int64_t sync_rx_local_ns) noexcept
        -> ServoOutput;

    /// Apply a phase step and reset the PI integrator when the
    /// phase error has exceeded the threshold for enough consecutive
    /// samples.
    auto apply_phase_jump(ServoOutput& out, int64_t phase_error_ns) noexcept -> ServoOutput;

    /// Running frequency offset in ppm, clamped to
    /// ±config.servo_ppm_limit. PI controller output.
    double current_ppm_{0.0};

    /// Number of consecutive samples above the jump threshold.
    uint8_t consecutive_phase_jumps_{0};

    /// Previous (master_ns, local_ns) pair for rate-ratio
    /// computation between two consecutive syncs.
    bool have_previous_{false};
    int64_t previous_master_ns_{0};
    int64_t previous_local_ns_{0};
};

}  // namespace statusbar::gptp
