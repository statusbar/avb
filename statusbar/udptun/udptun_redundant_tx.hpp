#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_monotonic_ring.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace statusbar::udptun {

/// Bounded ring of recent primary-stream sends, used to construct the
/// redundant copy: every TX opportunity at time T sends primary[seq=N]
/// and, in parallel, redundant carrying {seq, tx_gptp_ns} of the primary
/// packet sent X ms earlier.
class RedundantTxBuffer
{
  public:
    static constexpr size_t capacity = 1024;

    struct Entry
    {
        int64_t record_time_mraw_ns{0};  ///< monotonic when this primary was sent
        uint32_t sequence{0};            ///< primary's sequence number
        int64_t tx_gptp_ns{0};           ///< gPTP timestamp baked into the primary payload
    };

    /// Append a primary-send record.
    void record(int64_t record_time_mraw_ns, uint32_t sequence, int64_t tx_gptp_ns) noexcept
    {
        ring_.record(record_time_mraw_ns, Payload{.sequence = sequence, .tx_gptp_ns = tx_gptp_ns});
    }

    /// Return the entry whose record time is the largest <= target_ns.
    [[nodiscard]] auto entry_at_or_before(int64_t target_ns) const noexcept -> std::optional<Entry>
    {
        auto const e = ring_.entry_at_or_before(target_ns);
        if (!e) {
            return std::nullopt;
        }
        return Entry{
            .record_time_mraw_ns = e->timestamp_ns,
            .sequence = e->payload.sequence,
            .tx_gptp_ns = e->payload.tx_gptp_ns,
        };
    }

    [[nodiscard]] auto empty() const noexcept -> bool { return ring_.empty(); }
    [[nodiscard]] auto size() const noexcept -> size_t { return ring_.size(); }

  private:
    struct Payload
    {
        uint32_t sequence{0};
        int64_t tx_gptp_ns{0};
    };
    MonotonicRing<Payload, capacity> ring_;
};

}  // namespace statusbar::udptun
