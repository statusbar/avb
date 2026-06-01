// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_wire_clock.hpp"

#include "statusbar/gptp/gptp_time_bridge.hpp"
#include "statusbar/ptpclient/ptpclient_bridge.hpp"
#include "statusbar/test/test.hpp"

#include <cstdint>

using namespace statusbar;

TEST(wire_clock, empty_translator_falls_back_to_realtime)
{
    udptun::WireClock clock{};
    EXPECT_FALSE(static_cast<bool>(clock.translate));

    int64_t const before = udptun::realtime_ns();
    int64_t const w = clock.wire_ns(/*mraw_ns=*/12345);
    int64_t const after = udptun::realtime_ns();

    EXPECT_TRUE(w >= before);
    EXPECT_TRUE(w <= after);
}

TEST(wire_clock, gptp_slave_translator_forwards_to_to_gptp)
{
    gptp::GptpTimeBridge bridge{};
    constexpr int64_t app_anchor = 100'000;
    constexpr int64_t master_anchor = 1'000'000;
    bridge.get_app_time_ns = []() noexcept { return app_anchor; };
    bridge.update_from_anchor(/*rx_local_ns=*/0, /*tx_master_ns=*/master_anchor, /*rate_ratio=*/1.0);

    constexpr int64_t expected_offset = master_anchor - app_anchor;  // 900'000

    udptun::WireClock clock{.translate = udptun::make_gptp_slave_translator(bridge)};
    EXPECT_TRUE(static_cast<bool>(clock.translate));
    EXPECT_EQ(clock.wire_ns(0), expected_offset);
    EXPECT_EQ(clock.wire_ns(500'000), 500'000 + expected_offset);
    EXPECT_EQ(clock.wire_ns(-1'000'000), -1'000'000 + expected_offset);
}

// With a default-constructed PtpTimeBridge (rate=1.0, offset=0),
// `from_monotonic_ns(X) == X`. That gives the ptp4l translator a
// deterministic identity-ish mapping that lets us verify the structural
// raw↔mono offset adjustment without exercising the bridge's regression
// internals (covered by ptpclient_bridge_test).
TEST(wire_clock, ptp4l_translator_is_linear_modulo_clock_jitter)
{
    ptpclient::PtpTimeBridge bridge{};

    auto translator = udptun::make_ptp4l_translator(bridge);

    // The translator samples (mraw, mono) fresh on every call so the
    // raw↔mono offset stays current as phc2sys slews CLOCK_MONOTONIC.
    // Between consecutive calls the offset is essentially constant
    // (slewing × few-µs ≈ sub-femtosecond). Allow a small jitter
    // budget for clock_gettime read quantization between the two
    // samples within a single call.
    constexpr int64_t jitter_tol_ns = 1'000;  // 1 µs is generous

    int64_t const a = translator(1'000'000);
    int64_t const b = translator(1'000'000 + 50'000);
    int64_t const slope_ab = b - a;
    EXPECT_TRUE(slope_ab >= 50'000 - jitter_tol_ns);
    EXPECT_TRUE(slope_ab <= 50'000 + jitter_tol_ns);

    int64_t const c = translator(0);
    int64_t const slope_ac = a - c;
    EXPECT_TRUE(slope_ac >= 1'000'000 - jitter_tol_ns);
    EXPECT_TRUE(slope_ac <= 1'000'000 + jitter_tol_ns);
}

#if defined(__linux__)
// Linux-only: the make_ptp4l_translator implementation samples
// CLOCK_MONOTONIC and CLOCK_MONOTONIC_RAW, which is a Linux idiom —
// the offset between them is bounded by accumulated NTP slewing and
// tiny for short test runs. On macOS the same clock IDs map to
// mach_continuous_time vs mach_absolute_time and can diverge by
// arbitrary amounts across sleep/resume, so the 1-second bound does
// not hold. The translator itself isn't built for Darwin in
// production; this test would just confirm clock-divergence quirks.
TEST(wire_clock, ptp4l_translator_offset_is_small_in_typical_ranges)
{
    // The difference between CLOCK_MONOTONIC_RAW and CLOCK_MONOTONIC is
    // the accumulated NTP slewing of CLOCK_MONOTONIC since boot —
    // bounded by the kernel's max slew rate (~500 ppm) times the
    // uptime. A fixed second-level bound only works on short-uptime
    // systems; over ~24 h at full slew the delta can hit ~43 s. Scale
    // the bound with current uptime + a 1 s floor for sampling jitter
    // on near-boot systems.
    ptpclient::PtpTimeBridge bridge{};
    auto translator = udptun::make_ptp4l_translator(bridge);

    int64_t const mraw_now = udptun::monotonic_raw_ns();
    int64_t const got = translator(mraw_now);
    int64_t const diff = (got >= mraw_now) ? (got - mraw_now) : (mraw_now - got);

    constexpr int64_t max_ntp_slew_ppm = 500;
    constexpr int64_t floor_ns = 1'000'000'000;
    int64_t const max_drift_ns = ((mraw_now / 1'000'000) * max_ntp_slew_ppm) + floor_ns;
    EXPECT_TRUE(diff < max_drift_ns);
}
#endif

TEST_MAIN(statusbar_udptun, udptun_wire_clock_test)
