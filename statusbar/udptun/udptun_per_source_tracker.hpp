#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/ieee/ieee.hpp"
#include "statusbar/stats/stats_atomic_histogram.hpp"
#include "statusbar/udptun/udptun_stats.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace statusbar::udptun {

struct SourceState
{
    ieee::Eui64 sender_eui64{};
    std::unique_ptr<LatencyStats> stats{};  // unique_ptr — LatencyStats holds atomics (non-movable)
    uint32_t first_seq{0};
    uint32_t last_seq{0};
    int64_t first_seen_gptp_ns{0};
    int64_t last_seen_gptp_ns{0};
    uint32_t announced_interval_us{0};
    uint64_t received_count{0};
    uint64_t out_of_order_count{0};
    uint64_t duplicate_count{0};
    bool in_use{false};
};

/// Per-source latency tracker with capped storage and LRU eviction.
class PerSourceTracker
{
  public:
    PerSourceTracker(statusbar::stats::AtomicHistogramConfig const& cfg, size_t max_sources);

    /// Returns the SourceState for this sender (creates if new, evicts
    /// LRU by last_seen_gptp_ns if at capacity). Records `latency_ns`
    /// (the caller-supplied real one-way transit, *not* lateness vs
    /// the wire deadline) into the source's stats. `rx_gptp_ns` is used
    /// for last-seen / LRU bookkeeping. Returns nullptr only if
    /// max_sources == 0.
    auto observe(ieee::Eui64 sender, uint32_t seq, int64_t latency_ns, int64_t rx_gptp_ns, uint32_t interval_us) noexcept
        -> SourceState*;

    /// Returns all slots — callers filter on `s.in_use`. Used by report
    /// formatters which iterate active sources.
    [[nodiscard]] auto sources() const -> std::span<SourceState const>;

    [[nodiscard]] auto dropped_invalid() const noexcept -> uint64_t { return dropped_invalid_; }
    void increment_dropped_invalid() noexcept { ++dropped_invalid_; }

    /// Truncated datagrams: recvfrom reported a datagram larger than the
    /// drain buffer, so the application-visible bytes are an incomplete
    /// prefix and the codec can't decode them safely. Surfaces a wiring
    /// problem (peer sending jumbo frames at a buffer sized for standard
    /// MTU, or a path-MTU mismatch) rather than a decode-level failure.
    [[nodiscard]] auto truncated() const noexcept -> uint64_t { return truncated_; }
    void increment_truncated() noexcept { ++truncated_; }

  private:
    [[nodiscard]] auto find_existing(ieee::Eui64 const& s) noexcept -> SourceState*;
    [[nodiscard]] auto find_free_or_lru() noexcept -> SourceState*;

    statusbar::stats::AtomicHistogramConfig hist_cfg_;
    std::vector<SourceState> slots_;
    uint64_t dropped_invalid_{0};
    uint64_t truncated_{0};
};

}  // namespace statusbar::udptun
