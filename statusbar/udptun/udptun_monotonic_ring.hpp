#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/status/statusbar_assert.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace statusbar::udptun {

/// Bounded ring buffer of (timestamp_ns, payload) entries with backward
/// timestamp-keyed lookup. Caller is expected to feed monotonically
/// non-decreasing timestamps; entries are stored in record order and the
/// `at_or_before` lookup walks backward from the newest entry.
///
/// Used by the udptun TX path for two purposes:
///   - RedundantTxBuffer: keep recent primary sends so the redundant
///     stream can replay each primary `temporal_shift` ms later.
///   - TxHistory: keep recent (timestamp, sent_count) so the report path
///     can apply a "missing" grace window before declaring loss.
///
/// Lookup is O(N) backward-scan; in practice the answer is one of the
/// most recent entries (target = now - small_offset), so the loop exits
/// quickly. The ring itself is fixed-size so there are no allocations
/// after construction.
template <typename Payload, size_t N>
class MonotonicRing
{
  public:
    static constexpr size_t capacity = N;

    struct Entry
    {
        int64_t timestamp_ns{0};
        Payload payload{};
    };

    void record(int64_t timestamp_ns, Payload const& payload) noexcept
    {
        // Backward-scan lookup in entry_at_or_before relies on entries
        // being stored in non-decreasing timestamp order. The hot-path
        // sources (TX timer, RX timestamping) feed monotonic_raw_ns()
        // which is non-decreasing by definition; this assert catches a
        // wiring mistake in debug builds without paying for the check
        // in release.
        // clang-format off
        // Long STATUSBAR_ASSERT — clang-19 leaves it on one line, clang-22+
        // wants to wrap it. Wrap-off keeps both versions consistent.
        STATUSBAR_ASSERT((size_ == 0 || timestamp_ns >= last_recorded_ts_ns_) && "MonotonicRing: timestamps must be non-decreasing");
        // clang-format on
        entries_[next_write_] = Entry{.timestamp_ns = timestamp_ns, .payload = payload};
        last_recorded_ts_ns_ = timestamp_ns;
        next_write_ = (next_write_ + 1) % capacity;
        if (size_ < capacity) {
            ++size_;
        }
    }

    /// Return the entry whose timestamp is the largest <= target_ns, or
    /// nullopt if the ring is empty or the target predates every retained
    /// entry (the desired record has aged out).
    [[nodiscard]] auto entry_at_or_before(int64_t target_ns) const noexcept -> std::optional<Entry>
    {
        if (size_ == 0) {
            return std::nullopt;
        }
        for (size_t i = 0; i < size_; ++i) {
            size_t const idx = (next_write_ + capacity - 1 - i) % capacity;
            if (entries_[idx].timestamp_ns <= target_ns) {
                return entries_[idx];
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto empty() const noexcept -> bool { return size_ == 0; }
    [[nodiscard]] auto size() const noexcept -> size_t { return size_; }

  private:
    std::array<Entry, capacity> entries_{};
    size_t next_write_{0};
    size_t size_{0};
    int64_t last_recorded_ts_ns_{0};  ///< only consulted from the debug-build assert
};

}  // namespace statusbar::udptun
