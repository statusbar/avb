#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

//
// GptpTimeBridge — bidirectional conversion between gPTP master time
// and any application-side POSIX clock.
//
// Usage:
//
//   // In the event loop setup:
//   GptpTimeBridge bridge{
//       .get_gptp_time_ns = [&soft_clock]() { return soft_clock.get_ptp_time_ns(realtime_ns()); },
//       .get_app_time_ns  = []() { return monotonic_ns(); },
//   };
//   bridge.update();  // take initial cross-timestamp
//
//   // Periodically (every sync or every second):
//   bridge.update();
//
//   // Plain offset-based conversion (no rate compensation):
//   int64_t play_at_app = bridge.from_gptp(avtp_header.get_timestamp());
//
//   // Rate-aware conversion (uses both offset and the latest rate
//   // measurement to extrapolate from the anchor point — accurate
//   // to a few ns at any time within an update interval):
//   int64_t play_at_app = bridge.from_gptp_rated(avtp_header.get_timestamp());
//
// The bridge maintains a cached offset between the two clocks plus a
// rate-offset measurement, refreshed by calling update(). Between
// updates, the unrated offset drifts by at most (rate × interval); the
// rated conversions extrapolate from the anchor and hold sub-ns
// accuracy for typical drift rates and intervals up to a few seconds.
//
// Rate representation — int64 picoseconds-per-second:
//
//   rate_offset_ppt is (rate_ratio − 1) × 10¹², stored as int64. This
//   avoids the precision wart of representing a value very close to 1.0
//   in IEEE 754 double — most of a double's 52 mantissa bits would be
//   wasted on the constant `1.` and the leading zeros. With ppt:
//     - exact integer storage: no fractional accumulation
//     - native atomicity (8-byte aligned int64 is hardware-atomic on
//       aarch64 / x86_64)
//     - self-documenting: 1 ppt = 1 picosecond per second = 10⁻¹²
//     - range ±9.2 million ppm (way more than oscillator stability)
//   The double-returning rate_ratio() accessor is kept as a backward-
//   compat shim for callers that want the familiar 1.0±ε form.
//
// Thread safety: the four cached fields (offset, rate_offset, prev_app,
// prev_gptp) are published together under a single-writer seqlock
// (`rated_seq_`). Readers that need a consistent snapshot of the rated
// regression line (e.g. `to_gptp_rated` / `from_gptp_rated`) load all
// four under the seqlock; readers that only need the unrated offset
// (e.g. `to_gptp` / `from_gptp`) read `offset_ns_` directly with a
// single atomic load and are unaffected. The seqlock guards against
// the failure mode where a writer's update is observed mid-publish —
// in the worst case (a phase-jump update that bumps offset by the step
// size while leaving rate untouched), a torn read of OLD offset paired
// with NEW prev_a yields an error of exactly `−step`, i.e. potentially
// 100 ms+ for a real servo step.
//

#include "statusbar/itc/itc_atomic_triple_buffer.hpp"
#include "statusbar/sg14/inplace_function.h"

#include <atomic>
#include <cstdint>
#include <functional>

namespace statusbar::gptp {

struct GptpTimeBridge
{
    /// Function returning the current gPTP master time in nanoseconds.
    /// For the SoftClock path:
    ///   [&sc]() { return sc.get_ptp_time_ns(clock_realtime_ns()); }
    /// For the PHC path:
    ///   ops.get_local_time_ns  (the PHC IS the gPTP clock)
    statusbar::sg14::inplace_function<int64_t(), 64> get_gptp_time_ns;

    /// Function returning the current application-side clock in ns.
    /// Typically CLOCK_MONOTONIC or CLOCK_MONOTONIC_RAW:
    ///   []() {
    ///       struct timespec ts{};
    ///       clock_gettime(CLOCK_MONOTONIC, &ts);
    ///       return ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
    ///   }
    statusbar::sg14::inplace_function<int64_t(), 64> get_app_time_ns;

    /// Refresh the cached offset and rate. Call this from the event loop
    /// after each servo pass (every sync interval), or at least once
    /// per second. The first update() seeds the previous-sample
    /// storage; rate_offset_ppt() returns 0 (rate_ratio() returns 1.0)
    /// until the second update() arrives.
    void update() noexcept
    {
        if (!get_gptp_time_ns || !get_app_time_ns) {
            return;
        }
        // Read both clocks as close together as possible. The order
        // (app first, gptp second) minimizes the systematic bias: any
        // delay between the two reads is attributed to the offset,
        // which is corrected on the next update().
        int64_t const app = get_app_time_ns();
        int64_t const gptp = get_gptp_time_ns();

        int64_t new_ppt = rate_offset_ppt_.load(std::memory_order_relaxed);
        if (have_previous_) {
            int64_t const gptp_delta = gptp - prev_gptp_ns_.load(std::memory_order_relaxed);
            int64_t const app_delta = app - prev_app_ns_.load(std::memory_order_relaxed);
            int64_t const skew_ns = gptp_delta - app_delta;
            // Detect a clock step on the gPTP side. Real natural drift
            // is at most a few ms over an update interval (1000 ppm ×
            // 1 s = 1 ms); a skew of 100+ ms in a single update means
            // the gPTP clock was stepped (e.g. by the servo's
            // phase_jump path). Skip the rate computation for this
            // update so the rate doesn't spike — the previous rate
            // estimate stays valid because the natural oscillator
            // drift didn't actually change. Mirrors gptp_servo.cpp's
            // suppress_for_negative_time_jump pattern.
            constexpr int64_t step_detection_threshold_ns = 100'000'000;  // 100 ms
            bool const looks_like_step =
                (app_delta <= 0) || (skew_ns < -step_detection_threshold_ns) || (skew_ns > step_detection_threshold_ns);
            if (!looks_like_step) {
                // skew_ns is small by construction. The ratio
                // (skew × 1e12 / app_delta) is the rate offset in
                // ppt. Computed via double rather than int128 to stay
                // portable; the precision concern that motivated int64
                // ppt STORAGE doesn't apply here because the result
                // (a small correction value) is far from 1.0, so all
                // 15+ significant digits land in the meaningful range.
                double const ppt_d = (static_cast<double>(skew_ns) * 1e12) / static_cast<double>(app_delta);
                new_ppt = static_cast<int64_t>(ppt_d);
            }
        }
        // Publish the four-field rated snapshot atomically via the
        // triple buffer. Phase jumps would otherwise make a torn read
        // catastrophic: NEW offset arriving bumped by step size while
        // OLD prev_a remains would have a reader computing a
        // translation off by exactly the step size.
        rated_buffer_.publish(RatedSnapshot{
            .offset_ns = gptp - app,
            .ppt = new_ppt,
            .prev_gptp_ns = gptp,
            .prev_app_ns = app,
        });
        // Solo-atomic mirror of offset for the unrated to_gptp /
        // from_gptp readers (single load is race-free).
        offset_ns_.store(gptp - app, std::memory_order_relaxed);
        rate_offset_ppt_.store(new_ppt, std::memory_order_relaxed);
        prev_gptp_ns_.store(gptp, std::memory_order_relaxed);
        prev_app_ns_.store(app, std::memory_order_relaxed);
        have_previous_ = true;
    }

    /// Refresh the cached anchor + rate using a HW-stamped
    /// (local, master) pair from a Sync+FollowUp exchange. The
    /// app-clock anchor is sampled at this call from
    /// `get_app_time_ns`; the master-side anchor is the precise
    /// grandmaster TAI timestamp from the FollowUp body (with
    /// correctionField + rate-corrected link delay applied) — i.e.
    /// master TAI at Sync arrival.
    ///
    /// Preferred over `update()` for consumers that need HW-accurate
    /// gPTP→app conversion: the master anchor is taken directly from
    /// the wire pair instead of `get_gptp_time_ns()` sampled in user
    /// space some microseconds after the sync exchange completed.
    /// The app side remains user-space sampled (a few µs residual).
    ///
    /// `rate_ratio` is the servo's master/local rate estimate;
    /// stored as `(rate_ratio − 1) × 10¹²` in `rate_offset_ppt_`.
    /// `rx_local_ns` is currently unused (the app anchor comes from
    /// `get_app_time_ns`); kept in the signature so consumers that
    /// need to pair PHC and app timestamps can do so externally.
    void update_from_anchor(int64_t /*rx_local_ns*/, int64_t tx_master_ns, double rate_ratio) noexcept
    {
        if (!get_app_time_ns) {
            return;
        }
        int64_t const app = get_app_time_ns();
        int64_t const new_ppt = static_cast<int64_t>((rate_ratio - 1.0) * 1e12);
        rated_buffer_.publish(RatedSnapshot{
            .offset_ns = tx_master_ns - app,
            .ppt = new_ppt,
            .prev_gptp_ns = tx_master_ns,
            .prev_app_ns = app,
        });
        // Solo-atomic mirrors for diagnostics + unrated to_gptp readers.
        offset_ns_.store(tx_master_ns - app, std::memory_order_relaxed);
        rate_offset_ppt_.store(new_ppt, std::memory_order_relaxed);
        prev_gptp_ns_.store(tx_master_ns, std::memory_order_relaxed);
        prev_app_ns_.store(app, std::memory_order_relaxed);
        have_previous_ = true;
    }

    /// Refresh only the cached offset, NOT the rate or anchor
    /// timestamps. Useful when you want a current offset reading
    /// without disturbing the rate measurement — e.g. at program
    /// exit time, where calling full update() would compute a noisy
    /// rate from an arbitrary (possibly very small) interval since
    /// the last sync-driven update.
    void refresh_offset() noexcept
    {
        if (!get_gptp_time_ns || !get_app_time_ns) {
            return;
        }
        int64_t const app = get_app_time_ns();
        int64_t const gptp = get_gptp_time_ns();
        // Bump the seqlock around the offset write so a concurrent
        // rated reader sees a consistent (NEW offset, OLD prev_a/g,
        // OLD ppt) snapshot rather than risking a torn pair. The
        // resulting snapshot is intentionally NOT internally
        // consistent (offset is anchored to "now" while prev_a/g are
        // anchored to the last full update), but that's by design:
        // refresh_offset exists specifically to give callers a current
        // offset while preserving the rate-fit anchor. Rated readers
        // get the previous fit's parameters until the next update().
        // refresh_offset intentionally updates only the offset while
        // preserving the existing rate fit's anchor (prev_a/g, ppt).
        // Load the current snapshot, modify the offset, republish.
        rated_buffer_.publish(RatedSnapshot{
            .offset_ns = gptp - app,
            .ppt = rate_offset_ppt_.load(std::memory_order_relaxed),
            .prev_gptp_ns = prev_gptp_ns_.load(std::memory_order_relaxed),
            .prev_app_ns = prev_app_ns_.load(std::memory_order_relaxed),
        });
        offset_ns_.store(gptp - app, std::memory_order_relaxed);
    }

    /// Convert an application-clock timestamp to gPTP master time using
    /// only the cached offset (no rate compensation). Drift between
    /// updates is at most rate × interval-since-last-update.
    [[nodiscard]] auto to_gptp(int64_t app_ns) const noexcept -> int64_t
    {
        return app_ns + offset_ns_.load(std::memory_order_relaxed);
    }

    /// Convert a gPTP master timestamp to the application clock domain
    /// using only the cached offset (no rate compensation).
    [[nodiscard]] auto from_gptp(int64_t gptp_ns) const noexcept -> int64_t
    {
        return gptp_ns - offset_ns_.load(std::memory_order_relaxed);
    }

    /// Convert app→gptp using the cached rate offset to extrapolate
    /// from the most recent update's anchor point. Accurate to a few
    /// ns at any time within ~1 second of the last update for typical
    /// drift rates.
    ///
    /// Math:
    ///   app_delta_from_anchor = app_ns - prev_app_ns
    ///   correction_ns = app_delta * rate_offset_ppt / 10¹²
    ///   gptp = app_ns + offset + correction_ns
    ///
    /// The correction multiply uses double — overflow-safe for any
    /// realistic anchor age, and double precision is plenty because
    /// the correction value itself is small (µs–ms range, far from
    /// the value-near-1.0 wart that motivated int64 ppt STORAGE).
    [[nodiscard]] auto to_gptp_rated(int64_t app_ns) const noexcept -> int64_t
    {
        auto const snap = rated_buffer_.consume();
        int64_t const app_delta = app_ns - snap.prev_app_ns;
        double const correction_d = static_cast<double>(app_delta) * (static_cast<double>(snap.ppt) * 1e-12);
        return app_ns + snap.offset_ns + static_cast<int64_t>(correction_d);
    }

    /// Convert gptp→app using the cached rate offset. Linear
    /// approximation of the inverse rate; error ≤ 1 ns for ±1000 ppm
    /// drift over ≤ 1 s.
    ///
    /// Math (linear-approx inverse):
    ///   gptp_delta_from_anchor = gptp_ns - prev_gptp_ns
    ///   correction_ns = gptp_delta * rate_offset_ppt / 10¹²
    ///   app = gptp_ns - offset - correction_ns
    [[nodiscard]] auto from_gptp_rated(int64_t gptp_ns) const noexcept -> int64_t
    {
        auto const snap = rated_buffer_.consume();
        int64_t const gptp_delta = gptp_ns - snap.prev_gptp_ns;
        double const correction_d = static_cast<double>(gptp_delta) * (static_cast<double>(snap.ppt) * 1e-12);
        return gptp_ns - snap.offset_ns - static_cast<int64_t>(correction_d);
    }

    /// Get the current gPTP time (reads the gPTP source directly).
    [[nodiscard]] auto gptp_now() const noexcept -> int64_t { return get_gptp_time_ns ? get_gptp_time_ns() : 0; }

    /// Get the current application clock time.
    [[nodiscard]] auto app_now() const noexcept -> int64_t { return get_app_time_ns ? get_app_time_ns() : 0; }

    /// Current cached offset (gptp - app) in ns.
    [[nodiscard]] auto offset() const noexcept -> int64_t { return offset_ns_.load(std::memory_order_relaxed); }

    /// Current rate offset in picoseconds per second.
    /// rate_offset_ppt = (rate_ratio − 1) × 10¹².
    /// Returns 0 (perfect 1.0 ratio) if fewer than two updates have
    /// been taken.
    [[nodiscard]] auto rate_offset_ppt() const noexcept -> int64_t { return rate_offset_ppt_.load(std::memory_order_relaxed); }

    /// Rate ratio as a double (1.0 + rate_offset_ppt × 10⁻¹²).
    /// Backward-compat shim for callers that prefer the ratio form.
    /// New code should use rate_offset_ppt() — int64 has better
    /// precision and atomicity properties for values near 1.0.
    [[nodiscard]] auto rate_ratio() const noexcept -> double
    {
        return 1.0 + (static_cast<double>(rate_offset_ppt_.load(std::memory_order_relaxed)) * 1e-12);
    }

    /// Last anchor point's app-clock timestamp (the value of
    /// get_app_time_ns() at the most recent update() call). Used by
    /// to_gptp_rated / from_gptp_rated and exposed for diagnostics.
    [[nodiscard]] auto last_anchor_app_ns() const noexcept -> int64_t { return prev_app_ns_.load(std::memory_order_relaxed); }

    /// Last anchor point's gPTP timestamp.
    [[nodiscard]] auto last_anchor_gptp_ns() const noexcept -> int64_t { return prev_gptp_ns_.load(std::memory_order_relaxed); }

    /// Total publishes that overwrote a still-unconsumed prior publish
    /// in the rated buffer. Diagnostic — non-zero is expected on
    /// healthy systems because the sampler publishes faster than the
    /// RT-timer consumer reads.
    [[nodiscard]] auto rated_buffer_overruns() const noexcept -> uint64_t { return rated_buffer_.overruns(); }

    /// One consistent snapshot of the rated-conversion state.
    /// Published atomically by update / update_from_anchor /
    /// refresh_offset via rated_buffer_; consumed by the rated
    /// readers (to_gptp_rated / from_gptp_rated).
    struct RatedSnapshot
    {
        int64_t offset_ns;
        int64_t ppt;
        int64_t prev_gptp_ns;
        int64_t prev_app_ns;
    };

  private:
    // Solo atomic mirrors of the rated snapshot's fields. Kept so
    // unrated readers (to_gptp / from_gptp on offset_ns_) and the
    // diagnostic accessors (offset, rate_offset_ppt, last_anchor_*)
    // remain multi-reader safe via single atomic loads. The
    // multi-field rated readers go through rated_buffer_ instead.
    std::atomic<int64_t> offset_ns_{0};
    std::atomic<int64_t> rate_offset_ppt_{0};
    std::atomic<int64_t> prev_gptp_ns_{0};
    std::atomic<int64_t> prev_app_ns_{0};
    // Triple-buffered rated snapshot. Single writer (update /
    // update_from_anchor / refresh_offset, all called from the
    // event-loop thread); single consumer (the RT timer thread via
    // GptpSlaveClockSource). Replaces the previous rated_seq_ seqlock.
    mutable statusbar::itc::AtomicTripleBuffer<RatedSnapshot> rated_buffer_{};
    bool have_previous_{false};
};

}  // namespace statusbar::gptp
