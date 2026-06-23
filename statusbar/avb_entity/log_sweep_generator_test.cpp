// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Unit tests for LogSweepGenerator — the repeating logarithmic chirp fed as the
/// UDP tunnel test source.

#include "statusbar/avb_entity/log_sweep_generator.hpp"

#include "statusbar/test/test.hpp"

#include <cmath>
#include <cstdint>

using statusbar::avb_entity::LogSweepGenerator;

namespace {
auto approx(double a, double b, double tol) -> bool
{
    return std::fabs(a - b) <= tol;
}
}  // namespace

TEST(log_sweep, frequency_sweeps_log_start_to_end_and_repeats)
{
    LogSweepGenerator g;
    constexpr double FS = 96000.0;
    constexpr double DUR = 5.0;
    g.configure(/*f_start*/ 20.0, /*f_end*/ 1000.0, DUR, FS, /*amp*/ 0.5F);

    auto const period = static_cast<uint64_t>(DUR * FS);  // 480000

    // At t=0 the instantaneous frequency is f_start.
    EXPECT_TRUE(approx(g.current_frequency_hz(), 20.0, 0.01));

    // Advance to the geometric midpoint (t = DUR/2): log sweep => 20*sqrt(1000/20)
    // = 20*sqrt(50) ~= 141.42 Hz.
    for (uint64_t i = 0; i < period / 2; ++i) {
        (void)g.next();
    }
    EXPECT_TRUE(approx(g.current_frequency_hz(), 20.0 * std::sqrt(50.0), 0.5));

    // Advance to just before the end: frequency approaches f_end.
    for (uint64_t i = period / 2; i < period - 1; ++i) {
        (void)g.next();
    }
    EXPECT_TRUE(g.current_frequency_hz() > 980.0);
    EXPECT_TRUE(g.current_frequency_hz() <= 1000.5);

    // One more sample wraps the period back to f_start.
    (void)g.next();
    EXPECT_TRUE(approx(g.current_frequency_hz(), 20.0, 0.01));
}

TEST(log_sweep, output_bounded_by_amplitude_and_monotonic_freq_within_period)
{
    LogSweepGenerator g;
    constexpr double FS = 48000.0;
    g.configure(20.0, 1000.0, 1.0, FS, 0.5F);
    auto const period = static_cast<uint64_t>(1.0 * FS);

    double prev_f = g.current_frequency_hz();
    bool monotonic = true;
    float peak = 0.0F;
    for (uint64_t i = 0; i < period - 1; ++i) {
        float const s = g.next();
        peak = std::max(peak, std::fabs(s));
        double const f = g.current_frequency_hz();
        if (f < prev_f - 1e-9) {
            monotonic = false;
        }
        prev_f = f;
    }
    EXPECT_TRUE(monotonic);             // frequency rises across the sweep
    EXPECT_TRUE(peak <= 0.5F + 1e-6F);  // never exceeds amplitude
    EXPECT_TRUE(peak > 0.1F);           // actually produced signal
}

TEST(log_sweep, reset_returns_to_start)
{
    LogSweepGenerator g;
    g.configure(20.0, 1000.0, 5.0, 96000.0, 0.5F);
    for (int i = 0; i < 1000; ++i) {
        (void)g.next();
    }
    EXPECT_TRUE(g.current_frequency_hz() > 20.0);
    g.reset();
    EXPECT_TRUE(approx(g.current_frequency_hz(), 20.0, 0.01));
}

TEST_MAIN(statusbar_avb_entity, log_sweep_generator_test)
