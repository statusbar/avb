#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/container/container_presentation_slot_map.hpp"

#include <cstddef>
#include <cstdint>

namespace statusbar::udptun {

/// Per-(sender pair) coverage tracker for redundant streams, keyed by
/// presentation time (PT). The sender stamps each packet with
/// `PT = acquisition_wall + worst_case_latency`. The receiver uses PT as
/// the slot key in a fixed-capacity time-quantized ring; primary and
/// redundant copies of the same logical packet share the same PT and
/// therefore the same slot.
///
/// Use:
///   on_primary(pt_ns)             when a primary packet arrives
///   on_redundant(pt_ns)           when a redundant packet arrives
///   scan(latest_pt_ns, grace_ns)  periodically to retire slots whose PT
///                                 lies more than `grace_ns` behind the
///                                 latest observed PT
///
/// Reordering is implicit — out-of-order primaries land in the slot
/// indexed by their PT regardless of arrival order, so a late primary
/// flips the slot from "redundant only" to "primary received" without
/// any seq-gap bookkeeping.
///
/// Storage is `container::PresentationSlotMap<Coverage, capacity>`
/// (Capacity × (8 + 1) bytes ≈ 72 KB). Caller-supplied `slot_width_ns`
/// (= sender's tx_interval_ns) maps PTs to slot indices.
class RedundantRxTracker
{
  public:
    /// Slot map capacity. At slot_width = tx_interval = 1 ms this
    /// covers 8.192 s of recent PT history; at 100 ms intervals it
    /// covers 819 s. Scan must be called frequently enough that the
    /// retirement cursor advances at least one slot per `capacity`
    /// slot_widths of real time, otherwise wrap-around will overwrite
    /// un-retired slots and outcomes will be lost.
    static constexpr size_t capacity = 8192;

    /// @param slot_width_ns The sender's tx_interval_ns. Each PT-cycle
    ///        maps to one slot; the retirement cursor steps by this
    ///        amount.
    explicit RedundantRxTracker(int64_t slot_width_ns) noexcept;

    /// Record a primary arrival at presentation time `pt_ns`.
    /// Increments `primary_received()`. Idempotent within a slot —
    /// a duplicate primary on the same PT is dropped silently.
    void on_primary(int64_t pt_ns) noexcept;

    /// Record a redundant arrival at presentation time `pt_ns`.
    /// Returns true iff this is the first arrival of any kind for that
    /// PT — i.e. the redundant rescued a primary loss and the caller
    /// should feed the latency (lateness) sample into the latency
    /// stats. Returns false for "primary already won" (late duplicate)
    /// and "duplicate redundant".
    [[nodiscard]] auto on_redundant(int64_t pt_ns) noexcept -> bool;

    /// Walk the retirement cursor forward, finalizing every slot whose
    /// PT is at least `slot_width_ns_` past `(latest_pt_ns - grace_ns)`.
    /// Each retired slot tallies into one of: primary-only (already
    /// counted at receive — no extra count here), redundant-only
    /// (++recovered_), or empty (++true_loss_).
    void scan(int64_t latest_pt_ns, int64_t grace_ns) noexcept;

    /// Final flush. Walks the retirement cursor through every PT up to
    /// and including `end_pt_ns` (the sender's last stamped PT). Any
    /// expected slot left empty at end-of-run is counted as true loss.
    void flush_all(int64_t end_pt_ns) noexcept;

    [[nodiscard]] auto primary_received() const noexcept -> uint64_t { return primary_received_; }
    [[nodiscard]] auto redundant_received() const noexcept -> uint64_t { return redundant_received_; }
    [[nodiscard]] auto recovered() const noexcept -> uint64_t { return recovered_; }
    [[nodiscard]] auto true_loss() const noexcept -> uint64_t { return true_loss_; }
    [[nodiscard]] auto pending_count() const noexcept -> size_t { return map_.live_count(); }

  private:
    struct Coverage
    {
        bool primary_received{false};
        bool redundant_received{false};
    };

    [[nodiscard]] auto quantize(int64_t pt_ns) const noexcept -> int64_t { return (pt_ns / slot_width_ns_) * slot_width_ns_; }

    void retire_slot(int64_t pt_ns) noexcept;
    void initialize_cursor_if_needed(int64_t pt_ns) noexcept;

    container::PresentationSlotMap<Coverage, capacity> map_;
    int64_t slot_width_ns_;
    int64_t cursor_pt_{0};  ///< quantized PT of the next slot to retire
    bool cursor_initialized_{false};
    uint64_t primary_received_{0};
    uint64_t redundant_received_{0};
    uint64_t recovered_{0};
    uint64_t true_loss_{0};
};

}  // namespace statusbar::udptun
