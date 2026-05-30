#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// PTP Time Bridge module
/// Provides PtpTimeBridge class for mapping between PTP and monotonic clocks
/// Uses background sampling thread with linear regression for accurate time conversion

#include "statusbar/itc/itc_atomic_triple_buffer.hpp"
#include "statusbar/ptpclient/ptpclient_base.hpp"
#include "statusbar/realtime/realtime.hpp"
#include "statusbar/status/status.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <expected>
#include <functional>
#include <memory>
#include <system_error>
#include <thread>
#include <utility>

namespace statusbar::ptpclient {

using statusbar::failure;
using statusbar::Status;
using statusbar::StatusValue;
using statusbar::success;

// Type alias for time sample window using the generic realtime ring buffer
using TimingSampleWindow = realtime::TimingSampleWindow<TimeSample>;

//
// PtpTimeBridgeBase - Non-template base class for atomic operations
//

/// Base class that holds atomic members with out-of-line implementations
/// to prevent inlining issues across module boundaries (LLVM Issue #172241)
class PtpTimeBridgeBase
{
  public:
    PtpTimeBridgeBase() noexcept;

    /// Result of a seqlock-protected paired load of (rate, offset_ns).
    /// The two values were observed from the same writer publication,
    /// so the regression line `gptp = rate * mono + offset_ns` is
    /// internally consistent. A non-seqlocked load of one without the
    /// other is racy with the sampler thread's update_mapping: a
    /// 1 ppm slope wobble between samples combined with a stale
    /// intercept (or vice versa) yields a translation error of
    /// rate_delta * mono_now, which on a system that's been up for
    /// tens of hours is on the order of 100 ms per ppm.
    struct RateOffset
    {
        double rate;
        int64_t offset_ns;
    };

    /// Diagnostic: publishes that overwrote a still-unconsumed prior
    /// publish in the regression buffer. Non-zero is normal because
    /// the sampler thread publishes at ~500 Hz while the RT-timer
    /// consumer reads at ~8 kHz — the consumer just sees the latest
    /// value, and "missed" prior samples are counted here.
    [[nodiscard]] auto regression_buffer_overruns() const noexcept -> uint64_t;

  protected:
    // Running state
    [[nodiscard]] auto is_running_atomic() const noexcept -> bool;
    auto exchange_running(bool value) noexcept -> bool;
    auto set_running(bool value) noexcept -> void;

    // Reset requested
    [[nodiscard]] auto is_reset_requested_atomic() const noexcept -> bool;
    auto exchange_reset_requested(bool value) noexcept -> bool;
    auto set_reset_requested(bool value) noexcept -> void;

    /// Wait-free publish of the (rate, offset_ns) pair through the
    /// regression_buffer_ triple buffer. Single writer (the sampler
    /// thread); single consumer (the RT thread or main thread during
    /// setup/shutdown).
    auto regression_buffer_publish(double rate, int64_t offset_ns) noexcept -> void;

    /// Wait-free consume of the latest (rate, offset_ns). SPSC: only
    /// one consumer thread at a time may call this — the consumer
    /// position transfers between main (setup/shutdown) and the
    /// wan_timer / RT thread (steady-state operation).
    [[nodiscard]] auto regression_buffer_consume() const noexcept -> RateOffset;

    // Epoch
    [[nodiscard]] auto load_epoch() const noexcept -> uint64_t;
    auto increment_epoch() noexcept -> void;

    // Healthy
    [[nodiscard]] auto is_healthy_atomic() const noexcept -> bool;
    auto set_healthy(bool value) noexcept -> void;

    // Ever healthy
    [[nodiscard]] auto was_ever_healthy() const noexcept -> bool;
    auto set_ever_healthy(bool value) noexcept -> void;

    // RMS residual
    [[nodiscard]] auto load_rms_residual_ns() const noexcept -> int64_t;
    auto store_rms_residual_ns(int64_t value) noexcept -> void;

    // Worst bracket
    [[nodiscard]] auto load_worst_bracket_ns() const noexcept -> int64_t;
    auto store_worst_bracket_ns(int64_t value) noexcept -> void;

    // Sample count
    [[nodiscard]] auto load_sample_count() const noexcept -> int;
    auto store_sample_count(int value) noexcept -> void;

  private:
    // Thread control
    std::atomic<bool> running_;
    std::atomic<bool> reset_requested_;

    // Triple-buffered regression line. Single writer (update_mapping
    // in the sampler thread); single consumer (the RT timer thread,
    // plus the main thread during setup before the RT thread starts
    // and during shutdown after it exits). Initialized with
    // {rate=1.0, offset_ns=0} via the initial-value constructor so
    // the first consume() before any regression fit returns sensible
    // defaults. Replaces the previous (rate_, offset_ns_) +
    // rate_offset_seq_ seqlock.
    mutable statusbar::itc::AtomicTripleBuffer<RateOffset> regression_buffer_{RateOffset{.rate = 1.0, .offset_ns = 0}};
    std::atomic<uint64_t> epoch_;
    std::atomic<bool> healthy_;
    std::atomic<bool> ever_healthy_;
    std::atomic<int64_t> rms_residual_ns_;
    std::atomic<int64_t> worst_bracket_ns_;
    std::atomic<int> sample_count_;
};

//
// RAII Sampling Guard
//

class PtpTimeBridge;  // Forward declaration

/// RAII guard that stops sampling on destruction
class SamplingGuard
{
  public:
    SamplingGuard() noexcept = default;
    /// @param bridge Pointer to the PtpTimeBridge to guard
    explicit SamplingGuard(PtpTimeBridge* bridge) noexcept
        : bridge_{bridge}
    {}

    ~SamplingGuard() noexcept;

    // Move only
    SamplingGuard(SamplingGuard&& other) noexcept
        : bridge_{other.bridge_}
    {
        other.bridge_ = nullptr;
    }

    auto operator=(SamplingGuard&& other) noexcept -> SamplingGuard&
    {
        if (this != &other) {
            stop();
            bridge_ = other.bridge_;
            other.bridge_ = nullptr;
        }
        return *this;
    }

    SamplingGuard(SamplingGuard const&) = delete;
    auto operator=(SamplingGuard const&) -> SamplingGuard& = delete;

    /// Check if guard is active
    [[nodiscard]] auto active() const noexcept -> bool { return bridge_ != nullptr; }

    /// Release ownership without stopping
    auto release() noexcept -> PtpTimeBridge*
    {
        auto* b = bridge_;
        bridge_ = nullptr;
        return b;
    }

    /// Stop sampling explicitly
    void stop() noexcept;

  private:
    PtpTimeBridge* bridge_{nullptr};
};

//
// PTP Time Bridge
//

/// Bridge between PTP hardware clock and local monotonic clock
/// Maintains a continuously-updated linear mapping using background sampling
///
/// Usage:
///   PtpTimeBridge bridge;
///   auto guard = bridge.start_sampling(ptp_client, params);
///   // ... use bridge for conversions ...
///   // guard destructor calls stop_sampling()
class PtpTimeBridge : public PtpTimeBridgeBase
{
  public:
    /// Function type for reading PTP time (abstracted from hardware)
    using PtpTimeReader = std::function<StatusValue<int64_t>()>;

    PtpTimeBridge() noexcept = default;
    ~PtpTimeBridge() noexcept { stop_sampling(); }

    // No copy
    PtpTimeBridge(PtpTimeBridge const&) = delete;
    auto operator=(PtpTimeBridge const&) -> PtpTimeBridge& = delete;

    // No move (has thread)
    PtpTimeBridge(PtpTimeBridge&&) = delete;
    auto operator=(PtpTimeBridge&&) -> PtpTimeBridge& = delete;

    // Sampling Control

    /// Start background sampling with a PtpClientBase
    /// @param client PTP client to read time from (must remain valid while sampling)
    /// @param params Sampling configuration
    /// @return RAII guard that stops sampling on destruction, or error
    [[nodiscard]] auto start_sampling(PtpClientBase& client, BridgeSamplingParams const& params = {}) -> StatusValue<SamplingGuard>;

    /// Start background sampling with custom PTP time reader (for testing)
    /// @param ptp_reader Function to read PTP time
    /// @param params Sampling configuration
    /// @return RAII guard that stops sampling on destruction, or error
    [[nodiscard]] auto start_sampling(PtpTimeReader ptp_reader, BridgeSamplingParams const& params = {})
        -> StatusValue<SamplingGuard>;

    /// Stop background sampling
    void stop_sampling() noexcept;

    /// Check if sampling is running
    [[nodiscard]] auto is_running() const noexcept -> bool { return is_running_atomic(); }

    // Time Mapping State (lock-free reads)

    /// Get current time mapping state
    [[nodiscard]] auto get_mapping() const noexcept -> TimeMapping;

    /// Check if mapping is healthy
    [[nodiscard]] auto is_healthy() const noexcept -> bool { return is_healthy_atomic(); }

    /// Get current epoch (incremented on discontinuities)
    [[nodiscard]] auto epoch() const noexcept -> uint64_t { return load_epoch(); }

    // ClockSource-compatible interface (for use with ClockAdapter)

    /// Get current PTP time in nanoseconds
    /// This reads monotonic time and converts to PTP domain
    /// Uses __int128 precision to avoid floating-point errors with large offsets
    [[nodiscard]] auto now_ns() const noexcept -> int64_t;

    /// Convert PTP time to monotonic time (nanoseconds)
    /// Uses __int128 precision to avoid floating-point errors with large offsets
    /// @param ptp_ns PTP time in nanoseconds to convert
    [[nodiscard]] auto to_monotonic_ns(int64_t ptp_ns) const noexcept -> int64_t;

    /// Convert monotonic time to PTP time (nanoseconds)
    /// Uses __int128 precision to avoid floating-point errors with large offsets
    /// @param mono_ns Monotonic time in nanoseconds to convert
    [[nodiscard]] auto from_monotonic_ns(int64_t mono_ns) const noexcept -> int64_t;

    // Time Conversion (returning TimeConvertResult with epoch/health info)

    /// Convert PTP time to monotonic time
    /// Uses __int128 precision to avoid floating-point errors with large offsets
    /// @param ptp_ns PTP time in nanoseconds to convert
    [[nodiscard]] auto convert_ptp_to_monotonic(int64_t ptp_ns) const noexcept -> TimeConvertResult;

    /// Convert monotonic time to PTP time
    /// Uses __int128 precision to avoid floating-point errors with large offsets
    /// @param monotonic_ns Monotonic time in nanoseconds to convert
    [[nodiscard]] auto convert_monotonic_to_ptp(int64_t monotonic_ns) const noexcept -> TimeConvertResult;

    /// Prepare for sleep: validate state and convert PTP deadline to monotonic
    /// @return monotonic deadline in nanoseconds and pre-sleep mapping, or error
    [[nodiscard]] auto prepare_sleep(int64_t ptp_deadline_ns) noexcept -> StatusValue<std::pair<int64_t, TimeMapping>>;

    /// Finalize after sleep: validate epoch and convert wake time back to PTP
    /// @return current PTP time, or error if epoch changed or mapping unhealthy
    [[nodiscard]] auto finalize_wake(int64_t wake_mono, TimeMapping const& before) noexcept -> StatusValue<int64_t>;

    /// Sleep until a PTP deadline by converting to monotonic and sleeping
    /// @param ptp_deadline_ns Target PTP time in nanoseconds
    /// @return Current PTP time on success (allows caller to measure wake accuracy), error otherwise
    /// @note On Linux, uses clock_nanosleep for precision. On other platforms, uses std::this_thread::sleep_until.
    [[nodiscard]] auto sleep_until_ptp(int64_t ptp_deadline_ns) noexcept -> StatusValue<int64_t>;

    // Manual Epoch Control

    /// Force a new epoch (clears samples and marks unhealthy)
    void reset_epoch() noexcept;

  private:
    /// Read monotonic time
    /// On Linux: uses CLOCK_MONOTONIC
    /// On macOS: uses CLOCK_MONOTONIC
    [[nodiscard]] static auto read_monotonic_ns() noexcept -> int64_t;

    [[nodiscard]] auto start_sampling_impl(PtpTimeReader ptp_reader, BridgeSamplingParams const& params)
        -> StatusValue<SamplingGuard>;

    /// Take a single bracketed PTP/monotonic sample and process it
    /// @return true if a valid sample was collected, false if skipped
    auto collect_sample(TimingSampleWindow& window, int64_t& worst_bracket) -> bool;

    void sampler_loop();

    void update_mapping(TimingSampleWindow const& window, int64_t worst_bracket);

    // Configuration
    BridgeSamplingParams params_{};
    PtpTimeReader ptp_reader_;

    // Thread (atomics are in base class)
    std::thread sampler_thread_;
};

}  // namespace statusbar::ptpclient
