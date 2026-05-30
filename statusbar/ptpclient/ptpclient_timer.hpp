#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// PTP Timer module
/// Provides PtpTimer class for periodic wake-ups synchronized to PTP time

#include "statusbar/itc/itc_telemetry_counter.hpp"
#include "statusbar/ptpclient/ptpclient_base.hpp"
#include "statusbar/ptpclient/ptpclient_bridge.hpp"
#include "statusbar/realtime/realtime.hpp"
#include "statusbar/status/status.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <thread>
#include <type_traits>
#include <utility>

namespace statusbar::ptpclient {

using statusbar::failure;
using statusbar::Status;
using statusbar::StatusValue;
using statusbar::success;

//
// PtpTimerBase - Non-template base class for cross-thread state + telemetry
//

/// Base class that holds the atomic running flag and the recovery /
/// missed-cycle TelemetryCounters with out-of-line implementations,
/// to prevent inlining issues across module boundaries (LLVM Issue #172241)
class PtpTimerBase
{
  public:
    PtpTimerBase() noexcept;

  protected:
    // Running state
    [[nodiscard]] auto is_running_atomic() const noexcept -> bool;
    auto exchange_running(bool value) noexcept -> bool;
    auto set_running(bool value) noexcept -> void;

    // Recovery count
    [[nodiscard]] auto load_recovery_count() const noexcept -> int64_t;
    auto increment_recovery_count() noexcept -> void;
    auto reset_recovery_count() noexcept -> void;

    // Missed cycles
    [[nodiscard]] auto load_missed_cycles() const noexcept -> int64_t;
    auto add_missed_cycles(int64_t count) noexcept -> void;
    auto reset_missed_cycles() noexcept -> void;

    // For passing to monitor threads that need to stop when timer stops
    [[nodiscard]] auto running_flag_ptr() noexcept -> std::atomic<bool>* { return &running_; }

  private:
    std::atomic<bool> running_;
    statusbar::itc::TelemetryCounter<int64_t> recovery_count_{};
    statusbar::itc::TelemetryCounter<int64_t> missed_cycles_{};
};

//
// Timer Wake Info
//

/// Information passed to callback on each timer wake
struct TimerWakeInfo
{
    int64_t scheduled_time_ns;  ///< Target wake time (PTP nanoseconds)
    int64_t actual_time_ns;     ///< Actual wake time (PTP nanoseconds)
    int64_t error_ns;           ///< actual - scheduled (positive = late, negative = early)
    int64_t wake_count;         ///< Total successful wakes so far
};

//
// PTP Timer
//

/// Periodic timer synchronized to PTP time
///
/// @tparam Callback Callable type invoked on each wake with StatusValue<TimerWakeInfo> const&
///                  Can be a lambda, function pointer, or any callable object.
///
/// Usage:
///   PtpTimeBridge bridge;
///   auto guard = bridge.start_sampling(ptp_client, params);
///   // ... wait for healthy ...
///
///   auto timer = make_ptp_timer(bridge, 1'000'000,  // 1ms period
///       [](StatusValue<TimerWakeInfo> const& info) {
///           if (info) {
///               // Handle wake at info->scheduled_time_ns
///           } else {
///               // Handle error
///           }
///       });
///   timer.start();
///   // ... do other work ...
///   timer.stop();
template <typename Callback>
class PtpTimer : public PtpTimerBase
{
    static_assert(
        std::is_invocable_v<Callback, StatusValue<TimerWakeInfo> const&>,
        "Callback must be invocable with StatusValue<TimerWakeInfo> const&");

  public:
    /// Construct a PTP timer
    /// @param bridge Reference to PTP time bridge (must remain valid while timer is running)
    /// @param period_ns Wake period in nanoseconds
    /// @param callback Function called on each wake with StatusValue<TimerWakeInfo>
    /// @param compensation_ns Compensation offset in nanoseconds (negative = wake earlier)
    /// @param enable_realtime Enable realtime thread priority (SCHED_FIFO) for timer thread
    /// @param cpu_affinity CPU to pin timer thread to (-1 to disable)
    /// @param threshold Tripwire threshold in nanoseconds for detecting timer overruns
    PtpTimer(
        PtpTimeBridge& bridge,
        int64_t period_ns,
        Callback callback,
        int64_t compensation_ns = 0,
        bool enable_realtime = true,
        int cpu_affinity = 3,
        int64_t threshold = 50'000) noexcept(std::is_nothrow_move_constructible_v<Callback>)
        : bridge_{bridge}
        , period_ns_{period_ns}
        , compensation_ns_{compensation_ns}
        , enable_realtime_{enable_realtime}
        , cpu_affinity_{cpu_affinity}
        , threshold_{threshold}
        , callback_{std::move(callback)}
    {}

    ~PtpTimer() noexcept { stop(); }

    // No copy/move (has thread and references)
    PtpTimer(PtpTimer const&) = delete;
    auto operator=(PtpTimer const&) -> PtpTimer& = delete;
    PtpTimer(PtpTimer&&) = delete;
    auto operator=(PtpTimer&&) -> PtpTimer& = delete;

    /// Start the timer
    /// @return Success or error if timer is already running
    [[nodiscard]] auto start() -> Status
    {
        if (exchange_running(true)) {
            // Already running
            return failure(PtpError::bridge_not_running);  // Reuse error code
        }

        stats_.reset();
        reset_recovery_count();
        reset_missed_cycles();

        timer_thread_ = std::thread([this]() -> void { timer_loop(); });

        return success();
    }

    /// Stop the timer (blocks until thread joins)
    void stop() noexcept
    {
        bool const was_running = exchange_running(false);
        if (was_running && timer_thread_.joinable()) {
            timer_thread_.join();
        }
    }

    /// Check if timer is running
    [[nodiscard]] auto is_running() const noexcept -> bool { return is_running_atomic(); }

    /// Get a snapshot of wake statistics (thread-safe)
    /// @return Consistent copy of all statistics
    [[nodiscard]] auto stats() const noexcept -> stats::AtomicWakeStats::Snapshot { return stats_.snapshot(); }

    /// Get configured period in nanoseconds
    [[nodiscard]] auto period_ns() const noexcept -> int64_t { return period_ns_; }

    /// Get configured compensation in nanoseconds
    [[nodiscard]] auto compensation_ns() const noexcept -> int64_t { return compensation_ns_; }

    /// Check if realtime priority is enabled
    [[nodiscard]] auto realtime_enabled() const noexcept -> bool { return enable_realtime_; }

    /// Get the number of times the timer transitioned from healthy to unhealthy
    [[nodiscard]] auto recovery_count() const noexcept -> int64_t { return load_recovery_count(); }

    /// Get the total number of missed cycles (periods skipped due to running behind)
    [[nodiscard]] auto missed_cycles() const noexcept -> int64_t { return load_missed_cycles(); }

  private:
    /// Handle bridge error: notify callback, wait for recovery, realign
    void handle_bridge_error(std::error_code error, int64_t& next_wake)
    {
        increment_recovery_count();
        callback_(failure(error));

        while (is_running_atomic()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            auto const recovery = bridge_.convert_monotonic_to_ptp(read_monotonic_ns());
            if (recovery.healthy) {
                next_wake = ((recovery.time_ns / period_ns_) + 1) * period_ns_;
                break;
            }
        }
    }

    /// Process a successful wake: update stats, call callback, advance schedule
    /// @return wake error in nanoseconds
    auto process_wake(int64_t actual_wake_ptp, int64_t& next_wake) -> int64_t
    {
        int64_t const error_ns = actual_wake_ptp - next_wake;
        stats_.update(error_ns);

        TimerWakeInfo info{
            .scheduled_time_ns = next_wake, .actual_time_ns = actual_wake_ptp, .error_ns = error_ns, .wake_count = stats_.count()};
        callback_(success(info));

        next_wake += period_ns_;
        if (actual_wake_ptp > next_wake) {
            int64_t const periods_behind = ((actual_wake_ptp - next_wake) / period_ns_) + 1;
            next_wake += periods_behind * period_ns_;
            add_missed_cycles(periods_behind);
        }

        return error_ns;
    }

    void timer_loop()
    {
#if defined(__linux__)
        realtime::TraceConfig cfg;
        cfg.tracer = "wakeup_rt";  // while you're still sleep-based
        cfg.cpu = 3;
        cfg.buffer_kb = 32768;
        cfg.out_dir = "/tmp/statusbar_traces";

        realtime::TraceController controller(cfg);
        controller.start();  // likely needs root

        realtime::Tripwire tw;
        tw.threshold_ns = threshold_;

        // Spawn monitor thread — pass running flag so it stops when timer stops
        std::thread helper = realtime::monitor_tripwire(controller, tw, nullptr, running_flag_ptr());
#endif

        // Set realtime affinity if set (log failure but continue)
        if (cpu_affinity_ >= 0) {
            (void)realtime::set_realtime_affinity_logged(cpu_affinity_, "PTP timer");
        }
        // Set realtime priority if enabled (log failure but continue)
        if (enable_realtime_) {
            (void)realtime::set_realtime_priority_logged("PTP timer");
        }

        // Get initial PTP time and align to next period boundary
        auto const mapping = bridge_.get_mapping();
        if (!mapping.healthy) {
            // Bridge not healthy, call callback with error and exit
            callback_(failure(PtpError::bridge_not_healthy));
            return;
        }

        // Use bridge to convert current monotonic time to PTP time for initial alignment
        auto const monotonic_ns = read_monotonic_ns();
        auto const now_result = bridge_.convert_monotonic_to_ptp(monotonic_ns);
        int64_t const current_ptp = now_result.time_ns;
        int64_t next_wake = ((current_ptp / period_ns_) + 1) * period_ns_;
        int64_t next_monotonic_ns = monotonic_ns + (next_wake - current_ptp);

        while (is_running_atomic()) {
            auto wake_result = bridge_.sleep_until_ptp(next_wake + compensation_ns_);

            if (!wake_result) {
                if (!is_running_atomic()) {
                    break;
                }
                handle_bridge_error(wake_result.error(), next_wake);
                continue;
            }

            int64_t const error_ns = process_wake(*wake_result, next_wake);

#if defined(__linux__)
            if (error_ns > threshold_) {
                tw.trigger();
                set_running(false);
            }
#endif
        }
#if defined(__linux__)
        helper.join();
#endif
    }

    /// Read monotonic time in nanoseconds
    [[nodiscard]] static auto read_monotonic_ns() noexcept -> int64_t
    {
        auto const now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    }

    PtpTimeBridge& bridge_;
    int64_t period_ns_;
    int64_t compensation_ns_;
    bool enable_realtime_;
    int cpu_affinity_;
    int64_t threshold_;

    Callback callback_;
    // Atomics are in PtpTimerBase
    std::thread timer_thread_;
    stats::AtomicWakeStats stats_;
};

/// Factory function to create a PtpTimer with type deduction
/// @param bridge Reference to PTP time bridge
/// @param period_ns Wake period in nanoseconds
/// @param callback Function called on each wake
/// @param compensation_ns Compensation offset in nanoseconds (negative = wake earlier)
/// @param enable_realtime Enable realtime thread priority
/// @param cpu_affinity CPU to pin timer thread to (-1 to disable)
/// @param threshold Jitter threshold in nanoseconds for wake statistics
template <typename Callback>
[[nodiscard]] auto make_ptp_timer(
    PtpTimeBridge& bridge,
    int64_t period_ns,
    Callback&& callback,
    int64_t compensation_ns = 0,
    bool enable_realtime = true,
    int cpu_affinity = 3,
    int64_t threshold = 50'000) -> PtpTimer<std::decay_t<Callback>>
{
    return PtpTimer<std::decay_t<Callback>>{
        bridge, period_ns, std::forward<Callback>(callback), compensation_ns, enable_realtime, cpu_affinity, threshold};
}

}  // namespace statusbar::ptpclient
