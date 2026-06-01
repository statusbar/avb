// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_wire_clock.hpp"

#include "statusbar/gptp/gptp_time_bridge.hpp"
#include "statusbar/ptpclient/ptpclient_bridge.hpp"

namespace statusbar::udptun {

auto make_gptp_slave_translator(gptp::GptpTimeBridge const& bridge) -> WireClock::Translator
{
    return [&bridge](int64_t mraw_ns) noexcept -> int64_t { return bridge.to_gptp(mraw_ns); };
}

auto make_ptp4l_translator(ptpclient::PtpTimeBridge const& bridge) -> WireClock::Translator
{
    // CLOCK_MONOTONIC is slewed by adjtimex (phc2sys keeps it aligned
    // to the PHC), but CLOCK_MONOTONIC_RAW is not — so the offset
    // between them drifts over a session at the slewing rate (often
    // 1–20 ppm depending on the local oscillator's deviation from
    // nominal). A captured-once raw_offset would silently inject that
    // drift into every wire_ns translation, producing a slowly-walking
    // measured one-way latency. Sample both clocks fresh on every
    // call instead — two clock_gettime() syscalls is tens of ns, and
    // the sub-µs gap between them bounds any sampling skew.
    return [&bridge](int64_t mraw_ns) noexcept -> int64_t {
        timespec mraw_ts{};
        clock_gettime(CLOCK_MONOTONIC_RAW, &mraw_ts);
        timespec mono_ts{};
        clock_gettime(CLOCK_MONOTONIC, &mono_ts);
        int64_t const mraw_now = (mraw_ts.tv_sec * 1'000'000'000LL) + mraw_ts.tv_nsec;
        int64_t const mono_now = (mono_ts.tv_sec * 1'000'000'000LL) + mono_ts.tv_nsec;
        int64_t const mraw_to_mono_offset = mono_now - mraw_now;
        return bridge.from_monotonic_ns(mraw_ns + mraw_to_mono_offset);
    };
}

}  // namespace statusbar::udptun
