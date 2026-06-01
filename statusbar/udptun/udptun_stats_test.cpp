// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_stats.hpp"

#include "statusbar/test/test.hpp"

namespace {

using namespace statusbar;
using namespace statusbar::udptun;
using namespace statusbar::stats;

auto make_default_config() -> AtomicHistogramConfig
{
    return AtomicHistogramConfig{
        .low_ns = 0,
        .high_ns = 256'000,
        .bin_width_ns = 1'000,
    };
}

TEST(udptun_latency_stats, record_updates_both_collectors)
{
    LatencyStats s{make_default_config()};
    s.record(15'500);  // bucket 15, 15 µs
    s.record(20'000);  // bucket 20, 20 µs
    auto hist = s.histogram.snapshot();
    EXPECT_EQ(hist.total_count(), int64_t{2});
    auto ts = s.time_stats.snapshot();
    EXPECT_EQ(ts.count, int64_t{2});
    EXPECT_EQ(ts.min_ns, int64_t{15'500});
    EXPECT_EQ(ts.max_ns, int64_t{20'000});
}

TEST(udptun_percentile, p50_uniform)
{
    LatencyStats s{make_default_config()};
    // 100 samples uniformly across 1..100 µs.
    for (int i = 1; i <= 100; ++i) {
        s.record(static_cast<int64_t>(i) * 1'000);
    }
    auto snap = s.histogram.snapshot();
    int64_t const p50 = percentile_from_histogram(snap, 0.50);
    // P50 of 1..100 µs in 1 µs buckets → bucket index ≈ 49 → low_edge 49000.
    EXPECT_TRUE(p50 >= 48'000);
    EXPECT_TRUE(p50 <= 51'000);
}

TEST(udptun_percentile, handles_underflow)
{
    LatencyStats s{make_default_config()};
    s.record(-100);    // underflow
    s.record(50'000);  // normal
    auto snap = s.histogram.snapshot();
    int64_t const p25 = percentile_from_histogram(snap, 0.25);
    // 25th percentile lands in the underflow bucket → returns low_ns - 1.
    EXPECT_EQ(p25, int64_t{-1});
}

TEST(udptun_percentile, handles_overflow)
{
    LatencyStats s{make_default_config()};
    s.record(50'000);
    s.record(50'000);
    s.record(1'000'000);  // overflow (> 256 µs)
    auto snap = s.histogram.snapshot();
    int64_t const p99 = percentile_from_histogram(snap, 0.99);
    EXPECT_EQ(p99, int64_t{256'000});
}

TEST(udptun_percentile, empty_returns_zero)
{
    LatencyStats s{make_default_config()};
    auto snap = s.histogram.snapshot();
    EXPECT_EQ(percentile_from_histogram(snap, 0.50), int64_t{0});
}

}  // namespace

TEST_MAIN(statusbar_udptun, udptun_stats_test)
