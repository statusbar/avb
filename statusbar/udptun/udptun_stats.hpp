#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/stats/stats_atomic_histogram.hpp"
#include "statusbar/stats/stats_atomic_time_stats.hpp"

#include <cstdint>

namespace statusbar::udptun {

/// Per-source latency stats: 1024-bin atomic histogram + count/sum/min/max/variance.
/// 1024 bins covers the default 300 ms / 500 µs configuration (600 bins) with
/// headroom; the cap matches AtomicHistogramBase::max_bins.
struct LatencyStats
{
    statusbar::stats::AtomicHistogram<1024> histogram;
    statusbar::stats::AtomicTimeStats time_stats{};

    explicit LatencyStats(statusbar::stats::AtomicHistogramConfig const& cfg)
        : histogram{cfg}
    {}

    void record(int64_t latency_ns) noexcept
    {
        histogram.update(latency_ns);
        time_stats.update(latency_ns);
    }
};

/// Compute a percentile by walking cumulative bucket counts of a snapshot.
///
/// @param snap A snapshot of an AtomicHistogram<1024>.
/// @param p    Percentile in [0.0, 1.0].
/// @return The lower edge of the bucket whose cumulative count crosses
///         p × total. Returns:
///           - 0 if the snapshot is empty
///           - snap.config.low_ns - 1 if the percentile lands in the underflow bucket
///           - snap.config.high_ns if the percentile lands in the overflow bucket
[[nodiscard]] auto percentile_from_histogram(statusbar::stats::AtomicHistogram<1024>::Snapshot const& snap, double p) noexcept
    -> int64_t;

}  // namespace statusbar::udptun
