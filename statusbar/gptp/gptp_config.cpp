// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/gptp/gptp_config.hpp"

namespace statusbar::gptp {

auto GptpConfig::standard_defaults() noexcept -> GptpConfig
{
    GptpConfig cfg{};
    cfg.profile = Profile::Standard;
    // All other fields already carry the Standard profile defaults
    // via the struct's default member initializers.
    return cfg;
}

auto GptpConfig::avnu_automotive_slave_defaults() noexcept -> GptpConfig
{
    GptpConfig cfg{};
    cfg.profile = Profile::AvnuAutomotive;

    // Automotive sync interval — 31.25 ms — see OpenAvnu
    // ether_port.cpp:108-114.
    cfg.initial_log_sync_interval = -5;
    cfg.oper_log_sync_interval = -5;

    // Automotive Pdelay interval — 1 s (unchanged from standard),
    // but Pdelay may be skipped entirely for certain deployments.
    cfg.initial_log_pdelay_interval = 0;
    cfg.oper_log_pdelay_interval = 0;

    // Skip BMCA — forced slave role with fixed (logical) GM.
    cfg.bmca_enabled = false;

    // asCapable pre-enabled so the device can sync before the first
    // Pdelay exchange completes. OpenAvnu ether_port.cpp:106.
    cfg.as_capable_initial = true;

    // Do not require source port identity verification — the peer
    // link in automotive is trusted point-to-point.
    cfg.verify_source_port_identity = false;

    // Automotive accepts negative correction fields.
    cfg.allow_negative_correction_field = true;

    // Pdelay mode defaults to Active even in Automotive — caller
    // overrides to Disabled/RespondOnly and supplies manual delay
    // if the deployment needs it.
    cfg.pdelay_mode = PdelayMode::Active;

    // Automotive lock-fast: step the PHC after just 2 consecutive
    // Sync samples above the jump threshold, instead of the
    // standard-profile 6. Automotive deployments expect sync
    // convergence within tens of ms, not seconds.
    cfg.servo_phase_jump_consecutive_samples = 2;

    return cfg;
}

static auto are_log_intervals_valid(GptpConfig const& cfg) noexcept -> bool
{
    auto const in_range = [](int8_t v) noexcept -> bool { return v >= -7 && v <= 7; };
    return in_range(cfg.initial_log_sync_interval) && in_range(cfg.oper_log_sync_interval) &&
        in_range(cfg.initial_log_pdelay_interval) && in_range(cfg.oper_log_pdelay_interval) &&
        in_range(cfg.initial_log_announce_interval);
}

static auto are_servo_gains_valid(GptpConfig const& cfg) noexcept -> bool
{
    return cfg.servo_integral_gain >= 0.0 && cfg.servo_proportional_gain >= 0.0 && cfg.servo_ppm_limit > 0.0;
}

static auto are_capacity_limits_valid(GptpConfig const& cfg) noexcept -> bool
{
    return cfg.max_pending_sync > 0 && cfg.max_pending_pdelay > 0 && cfg.max_observers > 0;
}

auto GptpConfig::validate() const noexcept -> Status
{
    // Log interval range per IEEE 802.1AS-2020 Clause 10.6.2.1:
    // log2 intervals are constrained to [−7, +7] in practice.
    if (!are_log_intervals_valid(*this)) {
        return failure(make_error_code(GptpError::InvalidLogInterval));
    }

    // Timeout multipliers must be non-zero (IEEE 802.1AS requires >= 2).
    if (sync_receipt_timeout_multiplier < 2 || announce_receipt_timeout_multiplier < 2) {
        return failure(make_error_code(GptpError::InvalidConfiguration));
    }

    // Servo PI gains must be non-negative and finite.
    if (!are_servo_gains_valid(*this)) {
        return failure(make_error_code(GptpError::InvalidConfiguration));
    }

    // Capacity limits must be > 0.
    if (!are_capacity_limits_valid(*this)) {
        return failure(make_error_code(GptpError::InvalidConfiguration));
    }

    // neighborRateRatio sanity check: 0 or negative would destroy the
    // servo math.
    if (manual_neighbor_rate_ratio <= 0.0) {
        return failure(make_error_code(GptpError::InvalidConfiguration));
    }

    // neighbor_prop_delay_threshold_ns must be non-negative.
    if (neighbor_prop_delay_threshold_ns < 0) {
        return failure(make_error_code(GptpError::InvalidConfiguration));
    }

    return success();
}

}  // namespace statusbar::gptp
