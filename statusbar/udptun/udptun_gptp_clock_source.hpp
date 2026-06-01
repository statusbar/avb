#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Adapter that exposes a `gptp::SlaveSession`'s `GptpTimeBridge` through
/// the `statusbar::realtime::ClockSource` concept, so the udptun Session
/// can drive a `realtime::Timer<ClockAdapter<GptpClock<0>, ...>>` off the
/// same gPTP slave it already runs internally.
///
/// Linux only — depends on gptp::SlaveSession which is itself
/// Linux-only.

#if defined(__linux__)

#    include "statusbar/gptp/gptp_slave_session.hpp"

#    include <cstdint>

namespace statusbar::udptun {

/// Lightweight wrapper. Holds a non-owning pointer to the slave session;
/// caller keeps the session alive across the adapter's lifetime (the
/// session is a Session-owned member, so this is automatic for the
/// usual usage from inside udptun::Session).
class GptpSlaveClockSource
{
  public:
    explicit GptpSlaveClockSource(gptp::SlaveSession* session) noexcept
        : session_{session}
    {}

    [[nodiscard]] auto now_ns() const noexcept -> int64_t { return session_->bridge().gptp_now(); }

    [[nodiscard]] auto to_monotonic_ns(int64_t gptp_ns) const noexcept -> int64_t
    {
        return session_->bridge().from_gptp_rated(gptp_ns);
    }

    [[nodiscard]] auto from_monotonic_ns(int64_t mono_ns) const noexcept -> int64_t
    {
        return session_->bridge().to_gptp_rated(mono_ns);
    }

    [[nodiscard]] auto is_healthy() const noexcept -> bool { return session_->synced(); }

    /// gPTP slave doesn't currently expose a discontinuity counter; the
    /// realtime::Timer uses `epoch()` to detect time-stepping events
    /// and resync its deadline computation. Returning the slave's
    /// "is_synced" toggle as a bit is a coarse approximation: a flip
    /// from synced→unsynced→synced increments the epoch and the timer
    /// resyncs.
    [[nodiscard]] auto epoch() const noexcept -> uint64_t { return session_->synced() ? 1U : 0U; }

  private:
    gptp::SlaveSession* session_;
};

}  // namespace statusbar::udptun

#endif  // __linux__
