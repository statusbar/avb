#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Portable periodic-deadline timer. Replaces Linux-only `timerfd_*` use
/// in the udptun session loop. Holds no kernel resources — the next
/// firing time is computed from a baseline and an interval, and the
/// owning poll loop uses `next_deadline_ns - now` as its `poll()`
/// timeout. After `poll()` returns, the loop calls `consume_due` to
/// retire any pending ticks.

#include <algorithm>
#include <cstdint>

namespace statusbar::udptun {

/// A periodic deadline driven by monotonic time. Construct disabled by
/// default; call `arm` to set the interval and starting time.
class DeadlineTimer
{
  public:
    constexpr DeadlineTimer() noexcept = default;

    /// Set the period (in nanoseconds) and the wall time of the first
    /// firing. interval_ns == 0 leaves the timer disabled. After arm,
    /// `next_deadline_ns()` returns `first_fire_ns`.
    constexpr void arm(int64_t first_fire_ns, int64_t interval_ns) noexcept
    {
        interval_ns_ = interval_ns;
        next_ns_ = (interval_ns > 0) ? first_fire_ns : disabled_value;
    }

    /// True iff the timer is armed (interval_ns > 0).
    [[nodiscard]] constexpr auto armed() const noexcept -> bool { return interval_ns_ > 0; }

    /// Next deadline in monotonic-clock-ns. Returns disabled_value when
    /// disarmed.
    [[nodiscard]] constexpr auto next_deadline_ns() const noexcept -> int64_t { return next_ns_; }

    /// If `now_ns >= next_deadline_ns`, advance the deadline by one or
    /// more intervals (catching up if we missed several) and return the
    /// number of ticks consumed. Otherwise returns 0 without changes.
    [[nodiscard]] constexpr auto consume_due(int64_t now_ns) noexcept -> int
    {
        if (!armed() || now_ns < next_ns_) {
            return 0;
        }
        int64_t const overdue_ns = now_ns - next_ns_;
        int64_t const extra_ticks = overdue_ns / interval_ns_;
        int const total_ticks = static_cast<int>(extra_ticks) + 1;
        next_ns_ += interval_ns_ * static_cast<int64_t>(total_ticks);
        return total_ticks;
    }

    /// Disarm. consume_due / next_deadline_ns will report no work.
    constexpr void disarm() noexcept
    {
        interval_ns_ = 0;
        next_ns_ = disabled_value;
    }

    /// Sentinel returned by `next_deadline_ns` when disarmed.
    static constexpr int64_t disabled_value = INT64_MAX;

  private:
    int64_t interval_ns_{0};
    int64_t next_ns_{disabled_value};
};

/// Compute the `poll()` timeout in milliseconds that fires the soonest
/// among an arbitrary set of deadlines, clamped to `[0, max_ms]`. Pass
/// each timer's `next_deadline_ns()` plus a default ceiling. Returns
/// max_ms when there are no live deadlines.
[[nodiscard]] inline auto deadline_to_poll_timeout_ms(int64_t now_ns, int64_t earliest_deadline_ns, int max_ms) noexcept -> int
{
    if (earliest_deadline_ns >= DeadlineTimer::disabled_value) {
        return max_ms;
    }
    int64_t const remaining_ns = earliest_deadline_ns - now_ns;
    if (remaining_ns <= 0) {
        return 0;
    }
    int64_t const remaining_ms = (remaining_ns + 999'999) / 1'000'000;
    if (remaining_ms > static_cast<int64_t>(max_ms)) {
        return max_ms;
    }
    return static_cast<int>(remaining_ms);
}

}  // namespace statusbar::udptun
