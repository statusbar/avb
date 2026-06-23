// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for MediaClockRateTracker (avb_entity_media_rate.hpp): the
// entity-level glue that turns (gPTP-master, GPS CLOCK_REALTIME) samples into
// r = switch/GPS plus the gPTP->GPS-TAI mapping, with the GPS-sample and
// telemetry rate-limits. (The Kalman filter and TAI translator it wraps have
// their own ptpclient tests; this covers the offset/dt/r and rate-limit glue.)

#include "statusbar/avb_entity/avb_entity_media_rate.hpp"

#include "statusbar/test/test.hpp"

#include <cstdint>

using statusbar::avb_entity::MediaClockRateTracker;
namespace ptpclient = statusbar::ptpclient;

namespace {
constexpr uint64_t QUARTER_S = 250'000'000ULL;
constexpr uint64_t FIVE_S = 5'000'000'000ULL;
}  // namespace

TEST(media_rate, defaults_to_unity_and_no_tai)
{
    MediaClockRateTracker t;
    EXPECT_TRUE(t.r() == 1.0);
    EXPECT_FALSE(t.has_tai_sample());
}

TEST(media_rate, should_sample_rate_limits_to_quarter_second)
{
    MediaClockRateTracker t;
    EXPECT_TRUE(t.should_sample(1000));  // nothing sampled yet
    (void)t.add_sample(1000, 900);
    EXPECT_FALSE(t.should_sample(1000 + (QUARTER_S - 1)));  // < 0.25 s since last
    EXPECT_TRUE(t.should_sample(1000 + QUARTER_S));         // >= 0.25 s
}

TEST(media_rate, should_log_rate_limits_to_five_seconds)
{
    MediaClockRateTracker t;
    EXPECT_TRUE(t.should_log(7000));  // never logged yet
    t.mark_logged(7000);
    EXPECT_FALSE(t.should_log(7000 + (FIVE_S - 1)));
    EXPECT_TRUE(t.should_log(7000 + FIVE_S));
}

TEST(media_rate, tai_available_after_a_sample)
{
    MediaClockRateTracker t;
    EXPECT_FALSE(t.has_tai_sample());
    (void)t.add_sample(1'000'000'000, 1'000'000'000);
    (void)t.add_sample(1'000'000'000 + QUARTER_S, 1'000'000'000 + QUARTER_S);
    EXPECT_TRUE(t.has_tai_sample());
}

TEST(media_rate, tracks_positive_switch_vs_gps_rate)
{
    MediaClockRateTracker t;
    // gPTP (switch) runs ~50 ppm FASTER than GPS: the gptp-gps offset grows
    // 12500 ns each 0.25 s sample (= 50 us/s = 50 ppm), so r should settle just
    // above 1.0. We assert direction + a sane band, not the exact Kalman value.
    ptpclient::RatioEstimate est{};
    for (uint64_t i = 0; i < 60; ++i) {
        uint64_t const gptp = i * QUARTER_S;
        uint64_t const gps = gptp - (i * 12'500ULL);  // offset = +12500 ns/sample
        est = t.add_sample(gptp, gps);
    }
    EXPECT_TRUE(est.valid);
    EXPECT_TRUE(t.r() > 1.0);
    EXPECT_TRUE(t.r() < 1.001);  // ~1.00005 expected
}

TEST(media_rate, configure_resets_running_state)
{
    MediaClockRateTracker t;
    for (uint64_t i = 0; i < 30; ++i) {
        (void)t.add_sample(i * QUARTER_S, (i * QUARTER_S) - (i * 12'500ULL));
    }
    EXPECT_TRUE(t.r() != 1.0);

    t.configure(ptpclient::KalmanRatioTracker::Config{.meas_noise_ns = 1000.0, .jerk_psd = 1e-3});
    EXPECT_TRUE(t.r() == 1.0);
    EXPECT_TRUE(t.should_sample(1));  // last-sample timestamp cleared
}

// Test runner

TEST_MAIN(statusbar_avb_entity, avb_entity_media_rate_test)
