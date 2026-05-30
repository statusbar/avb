// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Implementation of PtpTimerBase
/// Separated from the header to keep atomic / TelemetryCounter operations
/// out-of-line across module boundaries

#include "statusbar/ptpclient/ptpclient_timer.hpp"

#include "statusbar/ptpclient/ptpclient.hpp"

#include <atomic>
#include <cstdint>

namespace statusbar::ptpclient {

//
// PtpTimerBase implementation
//

PtpTimerBase::PtpTimerBase() noexcept
    : running_(false)
{}

// Running state
auto PtpTimerBase::is_running_atomic() const noexcept -> bool
{
    return running_.load(std::memory_order_acquire);
}

auto PtpTimerBase::exchange_running(bool value) noexcept -> bool
{
    return running_.exchange(value, std::memory_order_acq_rel);
}

auto PtpTimerBase::set_running(bool value) noexcept -> void
{
    running_.store(value, std::memory_order_release);
}

// Recovery count
auto PtpTimerBase::load_recovery_count() const noexcept -> int64_t
{
    return recovery_count_.load();
}

auto PtpTimerBase::increment_recovery_count() noexcept -> void
{
    recovery_count_.add();
}

auto PtpTimerBase::reset_recovery_count() noexcept -> void
{
    recovery_count_.reset();
}

// Missed cycles
auto PtpTimerBase::load_missed_cycles() const noexcept -> int64_t
{
    return missed_cycles_.load();
}

auto PtpTimerBase::add_missed_cycles(int64_t count) noexcept -> void
{
    missed_cycles_.add(count);
}

auto PtpTimerBase::reset_missed_cycles() noexcept -> void
{
    missed_cycles_.reset();
}

}  // namespace statusbar::ptpclient