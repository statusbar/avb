#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_monotonic_ring.hpp"

#include <cstddef>
#include <cstdint>

namespace statusbar::udptun {

/// Bounded ring of (timestamp_ns, sent_count) snapshots of the TX side.
///
/// Used by the report path to compute "missing" packets with a grace window:
/// `sent_count_at_or_before(now_ns - grace_ns)` returns how many packets had
/// been sent by `cutoff = now - grace`. Packets sent after that cutoff are
/// still inside one round-trip from `now`, so a non-arrival doesn't yet imply
/// loss — it's pipeline depth.
///
/// A 4096-entry ring at 1 ms tx_interval covers ~4 s of history, far more
/// than any realistic 2 × max_rtt grace window. Sub-millisecond senders
/// shrink the coverage proportionally.
class TxHistory
{
  public:
    static constexpr size_t capacity = 4096;

    /// Append (timestamp_ns, sent_count). Caller passes monotonically
    /// non-decreasing timestamps.
    void record(int64_t timestamp_ns, uint64_t sent_count) noexcept { ring_.record(timestamp_ns, sent_count); }

    /// Return the largest sent_count whose recorded timestamp is <= cutoff_ns.
    /// Returns 0 if the history is empty or the cutoff predates every entry
    /// still in the ring (meaning the early sends have aged out).
    [[nodiscard]] auto sent_count_at_or_before(int64_t cutoff_ns) const noexcept -> uint64_t
    {
        auto const e = ring_.entry_at_or_before(cutoff_ns);
        return e ? e->payload : uint64_t{0};
    }

    [[nodiscard]] auto empty() const noexcept -> bool { return ring_.empty(); }
    [[nodiscard]] auto size() const noexcept -> size_t { return ring_.size(); }

  private:
    MonotonicRing<uint64_t, capacity> ring_;
};

}  // namespace statusbar::udptun
