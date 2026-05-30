#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

//
// SoftClock — virtual PTP clock for systems without PHC hardware.
//
// Maintains a running offset and frequency correction relative to a
// raw system clock (typically CLOCK_REALTIME on Linux). The servo's
// adjust_phase_ns / adjust_frequency_ppb calls update the virtual
// state instead of a hardware clock register. The application reads
// PTP time via get_ptp_time_ns().
//
// The key invariant: all timestamps entering the gPTP protocol stack
// must pass through correct_timestamp() first. This converts the raw
// kernel software timestamp (from SO_TIMESTAMPING cmsg) into the
// virtual clock domain, so the servo sees a consistent "local clock"
// that reflects its own previous corrections. Without this, the
// servo's integrator would wind up endlessly because the underlying
// timestamp source never actually changes rate.
//
// Thread safety: the write path (adjust_phase, adjust_frequency) is
// expected to run on the single event-loop thread that drives
// GptpSlavePort. The read path (get_ptp_time_ns, correct_timestamp)
// uses relaxed atomics so it can be safely called from an AVB stream
// scheduler thread that needs to compute presentation timestamps.
// The reads are individually atomic but not collectively consistent
// across offset+rate — for sub-microsecond coherence the caller
// should snapshot the offset+rate pair under a mutex, but for the
// ±1 ms accuracy of software timestamping this is not needed.
//

#include <atomic>
#include <cstdint>

namespace statusbar::gptp {

class SoftClock
{
  public:
    SoftClock() = default;

    /// Apply the virtual clock's accumulated corrections to a raw
    /// timestamp from the kernel (e.g. a CLOCK_REALTIME-based
    /// software timestamp from SO_TIMESTAMPING cmsg). Returns the
    /// timestamp in the "virtual PTP clock" domain.
    ///
    /// Must be called on every RX/TX timestamp BEFORE the value
    /// enters the GptpSlavePort's receive_frame or report_tx_timestamp.
    [[nodiscard]] auto correct_timestamp(int64_t raw_ns) const noexcept -> int64_t
    {
        int64_t const off = offset_ns_.load(std::memory_order_relaxed);
        double const rate = rate_ppb_.load(std::memory_order_relaxed);
        int64_t const base = rate_base_ns_.load(std::memory_order_relaxed);
        int64_t const elapsed = raw_ns - base;
        int64_t const rate_correction = static_cast<int64_t>(static_cast<double>(elapsed) * rate * 1e-9);
        return raw_ns + off + rate_correction;
    }

    /// Get the current best estimate of PTP master time. Reads the
    /// raw system clock and applies virtual corrections.
    ///
    /// @param raw_now_ns  Current reading from the raw system clock
    ///                    (e.g. CLOCK_REALTIME) in nanoseconds.
    [[nodiscard]] auto get_ptp_time_ns(int64_t raw_now_ns) const noexcept -> int64_t { return correct_timestamp(raw_now_ns); }

    /// Called by the servo's adjust_phase_ns callback. Steps the
    /// virtual clock forward (positive) or backward (negative) by
    /// the given number of nanoseconds.
    void adjust_phase(int64_t delta_ns, int64_t current_raw_ns) noexcept
    {
        // Freeze the rate-accumulated offset into the base offset so
        // the rate correction restarts cleanly from the new base point.
        freeze_rate_into_offset(current_raw_ns);
        offset_ns_.store(offset_ns_.load(std::memory_order_relaxed) + delta_ns, std::memory_order_relaxed);
    }

    /// Called by the servo's adjust_frequency_ppb callback. Sets the
    /// virtual clock's frequency offset relative to the raw system
    /// clock. Positive ppb means "my virtual clock runs faster than
    /// the raw clock" (PTP master is ahead, so speed up).
    void adjust_frequency(double ppb, int64_t current_raw_ns) noexcept
    {
        freeze_rate_into_offset(current_raw_ns);
        rate_ppb_.store(ppb, std::memory_order_relaxed);
    }

    /// Current virtual offset in ns (for diagnostic display).
    [[nodiscard]] auto offset_ns() const noexcept -> int64_t { return offset_ns_.load(std::memory_order_relaxed); }

    /// Current virtual rate in ppb (for diagnostic display).
    [[nodiscard]] auto rate_ppb() const noexcept -> double { return rate_ppb_.load(std::memory_order_relaxed); }

    /// Reset the virtual clock state (on link down, etc.).
    void reset() noexcept
    {
        offset_ns_.store(0, std::memory_order_relaxed);
        rate_ppb_.store(0.0, std::memory_order_relaxed);
        rate_base_ns_.store(0, std::memory_order_relaxed);
    }

  private:
    /// Collapse the accumulated rate correction into the base offset
    /// so that `correct_timestamp` returns the same value before and
    /// after the rate changes.
    void freeze_rate_into_offset(int64_t current_raw_ns) noexcept
    {
        double const rate = rate_ppb_.load(std::memory_order_relaxed);
        int64_t const base = rate_base_ns_.load(std::memory_order_relaxed);
        int64_t const elapsed = current_raw_ns - base;
        int64_t const accumulated = static_cast<int64_t>(static_cast<double>(elapsed) * rate * 1e-9);
        offset_ns_.store(offset_ns_.load(std::memory_order_relaxed) + accumulated, std::memory_order_relaxed);
        rate_base_ns_.store(current_raw_ns, std::memory_order_relaxed);
    }

    /// Additive offset from raw clock to virtual clock, in ns.
    std::atomic<int64_t> offset_ns_{0};

    /// Frequency correction in ppb (positive = virtual clock faster).
    std::atomic<double> rate_ppb_{0.0};

    /// Raw clock timestamp at which the current rate_ppb_ was last
    /// set. The rate correction is applied to the interval
    /// (raw_timestamp - rate_base_ns_).
    std::atomic<int64_t> rate_base_ns_{0};
};

}  // namespace statusbar::gptp
