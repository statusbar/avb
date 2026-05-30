// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Implementation of PtpTimeBridgeBase and PtpTimeBridge
/// Separated from module to prevent atomic operations from being inlined across module boundaries

#include "statusbar/ptpclient/ptpclient_bridge.hpp"

#include "statusbar/ptpclient/ptpclient.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <system_error>
#include <thread>
#include <utility>

#if defined(__linux__)
#    include <sched.h>
#endif

namespace statusbar::ptpclient {

//
// PtpTimeBridgeBase implementation
//

PtpTimeBridgeBase::PtpTimeBridgeBase() noexcept
    : running_(false)
    , reset_requested_(false)
    , epoch_(0)
    , healthy_(false)
    , ever_healthy_(false)
    , rms_residual_ns_(0)
    , worst_bracket_ns_(0)
    , sample_count_(0)
{}

auto PtpTimeBridgeBase::regression_buffer_publish(double const rate, int64_t const offset_ns) noexcept -> void
{
    regression_buffer_.publish(RateOffset{.rate = rate, .offset_ns = offset_ns});
}

auto PtpTimeBridgeBase::regression_buffer_consume() const noexcept -> RateOffset
{
    return regression_buffer_.consume();
}

auto PtpTimeBridgeBase::regression_buffer_overruns() const noexcept -> uint64_t
{
    return regression_buffer_.overruns();
}

// Running state
auto PtpTimeBridgeBase::is_running_atomic() const noexcept -> bool
{
    return running_.load(std::memory_order_relaxed);
}

auto PtpTimeBridgeBase::exchange_running(bool value) noexcept -> bool
{
    return running_.exchange(value, std::memory_order_relaxed);
}

auto PtpTimeBridgeBase::set_running(bool value) noexcept -> void
{
    running_.store(value, std::memory_order_relaxed);
}

// Reset requested
auto PtpTimeBridgeBase::is_reset_requested_atomic() const noexcept -> bool
{
    return reset_requested_.load(std::memory_order_relaxed);
}

auto PtpTimeBridgeBase::exchange_reset_requested(bool value) noexcept -> bool
{
    return reset_requested_.exchange(value, std::memory_order_relaxed);
}

auto PtpTimeBridgeBase::set_reset_requested(bool value) noexcept -> void
{
    reset_requested_.store(value, std::memory_order_relaxed);
}

// Epoch
auto PtpTimeBridgeBase::load_epoch() const noexcept -> uint64_t
{
    return epoch_.load(std::memory_order_relaxed);
}

auto PtpTimeBridgeBase::increment_epoch() noexcept -> void
{
    epoch_.fetch_add(1, std::memory_order_relaxed);
}

// Healthy
auto PtpTimeBridgeBase::is_healthy_atomic() const noexcept -> bool
{
    return healthy_.load(std::memory_order_relaxed);
}

auto PtpTimeBridgeBase::set_healthy(bool value) noexcept -> void
{
    healthy_.store(value, std::memory_order_relaxed);
}

// Ever healthy
auto PtpTimeBridgeBase::was_ever_healthy() const noexcept -> bool
{
    return ever_healthy_.load(std::memory_order_relaxed);
}

auto PtpTimeBridgeBase::set_ever_healthy(bool value) noexcept -> void
{
    ever_healthy_.store(value, std::memory_order_relaxed);
}

// RMS residual
auto PtpTimeBridgeBase::load_rms_residual_ns() const noexcept -> int64_t
{
    return rms_residual_ns_.load(std::memory_order_relaxed);
}

auto PtpTimeBridgeBase::store_rms_residual_ns(int64_t value) noexcept -> void
{
    rms_residual_ns_.store(value, std::memory_order_relaxed);
}

// Worst bracket
auto PtpTimeBridgeBase::load_worst_bracket_ns() const noexcept -> int64_t
{
    return worst_bracket_ns_.load(std::memory_order_relaxed);
}

auto PtpTimeBridgeBase::store_worst_bracket_ns(int64_t value) noexcept -> void
{
    worst_bracket_ns_.store(value, std::memory_order_relaxed);
}

// Sample count
auto PtpTimeBridgeBase::load_sample_count() const noexcept -> int
{
    return sample_count_.load(std::memory_order_relaxed);
}

auto PtpTimeBridgeBase::store_sample_count(int value) noexcept -> void
{
    sample_count_.store(value, std::memory_order_relaxed);
}

//
// SamplingGuard implementation
//

SamplingGuard::~SamplingGuard() noexcept
{
    stop();
}

void SamplingGuard::stop() noexcept
{
    if (bridge_ != nullptr) {
        bridge_->stop_sampling();
        bridge_ = nullptr;
    }
}

//
// PtpTimeBridge implementation
//

auto PtpTimeBridge::start_sampling(PtpClientBase& client, BridgeSamplingParams const& params) -> StatusValue<SamplingGuard>
{
    if (!client.is_open()) {
        return failure(PtpError::device_not_open);
    }

    // Create reader that captures client reference
    auto ptp_reader = [&client]() -> StatusValue<int64_t> { return client.get_time_ns(); };

    return start_sampling_impl(std::move(ptp_reader), params);
}

auto PtpTimeBridge::start_sampling(PtpTimeReader ptp_reader, BridgeSamplingParams const& params) -> StatusValue<SamplingGuard>
{
    return start_sampling_impl(std::move(ptp_reader), params);
}

void PtpTimeBridge::stop_sampling() noexcept
{
    bool const was_running = exchange_running(false);
    if (was_running && sampler_thread_.joinable()) {
        sampler_thread_.join();
    }
}

auto PtpTimeBridge::now_ns() const noexcept -> int64_t
{
    int64_t const mono_ns = read_monotonic_ns();
    auto const ro = regression_buffer_consume();
    // Use precise conversion with __int128 arithmetic
    auto const slope = FixedPointSlope::from_double(ro.rate);
    return monotonic_to_ptp_precise(mono_ns, slope, ro.offset_ns);
}

auto PtpTimeBridge::to_monotonic_ns(int64_t ptp_ns) const noexcept -> int64_t
{
    auto const ro = regression_buffer_consume();
    // Use precise conversion with __int128 arithmetic
    auto const slope = FixedPointSlope::from_double(ro.rate);
    return ptp_to_monotonic_precise(ptp_ns, slope, ro.offset_ns);
}

auto PtpTimeBridge::from_monotonic_ns(int64_t mono_ns) const noexcept -> int64_t
{
    auto const ro = regression_buffer_consume();
    // Use precise conversion with __int128 arithmetic
    auto const slope = FixedPointSlope::from_double(ro.rate);
    return monotonic_to_ptp_precise(mono_ns, slope, ro.offset_ns);
}

auto PtpTimeBridge::prepare_sleep(int64_t ptp_deadline_ns) noexcept -> StatusValue<std::pair<int64_t, TimeMapping>>
{
    if (!is_running_atomic()) {
        return failure(PtpError::bridge_not_running);
    }

    auto const before = get_mapping();
    if (!before.healthy) {
        return failure(PtpError::bridge_not_healthy);
    }

    auto const slope = FixedPointSlope::from_double(before.rate);
    int64_t const offset_ns = static_cast<int64_t>(before.offset_ns);
    int64_t const mono_deadline = ptp_to_monotonic_precise(ptp_deadline_ns, slope, offset_ns);

    return success(std::pair{mono_deadline, before});
}

auto PtpTimeBridge::finalize_wake(int64_t wake_mono, TimeMapping const& before) noexcept -> StatusValue<int64_t>
{
    auto const after = get_mapping();
    if (after.epoch != before.epoch) {
        return failure(PtpError::bridge_epoch_changed);
    }
    if (!after.healthy) {
        return failure(PtpError::bridge_not_healthy);
    }

    auto const slope = FixedPointSlope::from_double(after.rate);
    int64_t const offset_ns = static_cast<int64_t>(after.offset_ns);
    return success(monotonic_to_ptp_precise(wake_mono, slope, offset_ns));
}

void PtpTimeBridge::reset_epoch() noexcept
{
    increment_epoch();
    set_healthy(false);
    set_ever_healthy(false);
    set_reset_requested(true);
}

auto PtpTimeBridge::read_monotonic_ns() noexcept -> int64_t
{
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return timespec_to_ns(ts.tv_sec, ts.tv_nsec);
}

auto PtpTimeBridge::start_sampling_impl(PtpTimeReader ptp_reader, BridgeSamplingParams const& params) -> StatusValue<SamplingGuard>
{
    if (exchange_running(true)) {
        // Already running
        return failure(PtpError::bridge_not_running);
    }

    params_ = params;
    ptp_reader_ = std::move(ptp_reader);
    set_reset_requested(false);

    sampler_thread_ = std::thread([this]() -> void { sampler_loop(); });

    return success(SamplingGuard{this});
}

void PtpTimeBridge::sampler_loop()
{
    TimingSampleWindow window(static_cast<size_t>(params_.window_size));

    auto const period = std::chrono::nanoseconds(1'000'000'000LL / std::max(1, params_.sample_hz));
    auto next = std::chrono::steady_clock::now();

    int64_t worst_bracket = 0;

    while (is_running_atomic()) {
        next += period;
        collect_sample(window, worst_bracket);
        std::this_thread::sleep_until(next);
    }
}

auto PtpTimeBridge::get_mapping() const noexcept -> TimeMapping
{
    TimeMapping m;
    auto const ro = regression_buffer_consume();
    m.rate = ro.rate;
    m.offset_ns = ro.offset_ns;
    m.epoch = load_epoch();
    m.healthy = is_healthy_atomic();
    m.rms_residual_ns = load_rms_residual_ns();
    m.worst_bracket_ns = load_worst_bracket_ns();
    m.sample_count = load_sample_count();
    return m;
}

auto PtpTimeBridge::convert_ptp_to_monotonic(int64_t ptp_ns) const noexcept -> TimeConvertResult
{
    TimeConvertResult result;
    result.epoch = load_epoch();
    result.healthy = is_healthy_atomic();

    auto const ro = regression_buffer_consume();
    auto const slope = FixedPointSlope::from_double(ro.rate);
    result.time_ns = ptp_to_monotonic_precise(ptp_ns, slope, ro.offset_ns);

    return result;
}

auto PtpTimeBridge::convert_monotonic_to_ptp(int64_t monotonic_ns) const noexcept -> TimeConvertResult
{
    TimeConvertResult result;
    result.epoch = load_epoch();
    result.healthy = is_healthy_atomic();

    auto const ro = regression_buffer_consume();
    auto const slope = FixedPointSlope::from_double(ro.rate);
    result.time_ns = monotonic_to_ptp_precise(monotonic_ns, slope, ro.offset_ns);

    return result;
}

auto PtpTimeBridge::sleep_until_ptp(int64_t ptp_deadline_ns) noexcept -> StatusValue<int64_t>
{
    auto prep = prepare_sleep(ptp_deadline_ns);
    if (!prep) {
        return forward_failure(prep);
    }
    auto const [mono_deadline, before] = *prep;

#if defined(__linux__)
    // Sleep using CLOCK_MONOTONIC (CLOCK_MONOTONIC_RAW not supported with clock_nanosleep)
    int64_t sec = 0, nsec = 0;
    ns_to_timespec(mono_deadline, sec, nsec);
    timespec ts{};
    ts.tv_sec = static_cast<time_t>(sec);
    ts.tv_nsec = static_cast<long>(nsec);

    int const rc = ::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
    if (rc == EINTR) {
        // Signal received -- return error so caller can check shutdown flag
        return failure(std::error_code(EINTR, std::system_category()));
    }

    if (rc != 0) {
        return failure(std::error_code(rc, std::system_category()));
    }

    // Optional spin refinement
    if (params_.wake_refine_spin_ns > 0) {
        while (true) {
            timespec now_ts;
            ::clock_gettime(CLOCK_MONOTONIC, &now_ts);
            int64_t const now = timespec_to_ns(now_ts.tv_sec, now_ts.tv_nsec);
            if (now >= mono_deadline) {
                break;
            }
            if (mono_deadline - now > 10'000) {
                ::sched_yield();
            }
        }
    }

    timespec wake_ts;
    ::clock_gettime(CLOCK_MONOTONIC, &wake_ts);
    int64_t const wake_mono = timespec_to_ns(wake_ts.tv_sec, wake_ts.tv_nsec);
#else
    // Cross-platform fallback -- less precise than Linux clock_nanosleep
    auto const deadline = std::chrono::steady_clock::time_point{std::chrono::nanoseconds{mono_deadline}};
    std::this_thread::sleep_until(deadline);
    int64_t const wake_mono =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif

    return finalize_wake(wake_mono, before);
}

auto PtpTimeBridge::collect_sample(TimingSampleWindow& window, int64_t& worst_bracket) -> bool
{
    if (exchange_reset_requested(false)) {
        window.clear();
        worst_bracket = 0;
    }

    int64_t const r1 = read_monotonic_ns();
    auto ptp_result = ptp_reader_();
    int64_t const r2 = read_monotonic_ns();

    if (!ptp_result) {
        return false;
    }

    int64_t const ptp = *ptp_result;
    int64_t bracket = r2 - r1;
    if (bracket < 0) {
        bracket = -bracket;
    }
    if (bracket > worst_bracket) {
        worst_bracket = bracket;
    }

    if (bracket > params_.max_bracket_ns) {
        return true;
    }

    int64_t const mono_mid = (r1 + r2) / 2;
    window.push({.monotonic_ns = mono_mid, .ptp_ns = ptp, .bracket_ns = bracket});

    size_t const min_samples = static_cast<size_t>(std::max(2, std::min(params_.min_samples_for_healthy, params_.window_size / 4)));

    if (window.size() >= min_samples) {
        update_mapping(window, worst_bracket);
    }

    return true;
}

void PtpTimeBridge::update_mapping(TimingSampleWindow const& window, int64_t worst_bracket)
{
    auto fit = fit_time_mapping(window, params_.max_rate_ppm);

    if (!fit.valid) {
        return;
    }

    // Detect step/discontinuity - but only after we've established a healthy mapping
    // During initial convergence, large residuals are expected as the fit stabilizes.
    // Step detection is meant to catch sudden clock jumps after we have a good mapping.
    if (was_ever_healthy()) {
        if (fit.max_residual > static_cast<double>(params_.step_threshold_ns)) {
            // Step detected - reset epoch and clear samples
            increment_epoch();
            set_healthy(false);
            set_ever_healthy(false);  // Must re-establish health
            set_reset_requested(true);
            store_rms_residual_ns(static_cast<int64_t>(fit.rms_residual));
            store_worst_bracket_ns(worst_bracket);
            return;
        }
    }

    // Publish the (rate, offset_ns) pair atomically via the triple
    // buffer. The regression line `gptp = rate * mono + offset_ns`
    // must be observed as a consistent pair: a torn read (new slope
    // with old intercept, or vice versa) produces an error of
    // Δslope * mono_now, which on a system that's been up tens of
    // hours (mono_now ≈ 2*10^14 ns) means a 1-ppm slope wobble would
    // translate to a ~200 ms gptp glitch.
    regression_buffer_publish(fit.slope, fit.intercept_ns);
    store_rms_residual_ns(static_cast<int64_t>(fit.rms_residual));
    store_worst_bracket_ns(worst_bracket);
    store_sample_count(static_cast<int>(window.size()));

    // Determine health
    bool const healthy = fit.rms_residual <= static_cast<double>(params_.degrade_threshold_ns);
    set_healthy(healthy);
    if (healthy) {
        set_ever_healthy(true);
    }
}

}  // namespace statusbar::ptpclient
