// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_stats.hpp"

#include <algorithm>

namespace statusbar::udptun {

auto percentile_from_histogram(statusbar::stats::AtomicHistogram<1024>::Snapshot const& snap, double p) noexcept -> int64_t
{
    int64_t const total = snap.total_count();
    if (total <= 0) {
        return 0;
    }
    p = std::clamp(p, 0.0, 1.0);
    int64_t const target = static_cast<int64_t>(static_cast<double>(total) * p);

    int64_t cumulative = snap.underflow;
    if (cumulative > target) {
        return snap.config.low_ns - 1;
    }
    for (size_t i = 0; i < snap.num_bins; ++i) {
        cumulative += snap.bins[i];
        if (cumulative > target) {
            return snap.bin_low(i);
        }
    }
    return snap.config.high_ns;  // landed in overflow bucket
}

}  // namespace statusbar::udptun
