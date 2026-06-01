// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// clang-format off
// Include order differs between clang-19 and clang-22 here — wrap-off
// pins it so both versions produce the same layout.
#include "statusbar/udptun/udptun_redundant_rx.hpp"
#include "statusbar/status/statusbar_assert.hpp"
// clang-format on

namespace statusbar::udptun {

RedundantRxTracker::RedundantRxTracker(int64_t slot_width_ns) noexcept
    : map_{slot_width_ns}
    , slot_width_ns_{slot_width_ns}
{
    STATUSBAR_ASSERT(slot_width_ns_ > 0 && "RedundantRxTracker: slot_width_ns must be > 0");
}

void RedundantRxTracker::initialize_cursor_if_needed(int64_t pt_ns) noexcept
{
    if (cursor_initialized_) {
        return;
    }
    cursor_pt_ = quantize(pt_ns);
    cursor_initialized_ = true;
}

void RedundantRxTracker::on_primary(int64_t pt_ns) noexcept
{
    initialize_cursor_if_needed(pt_ns);
    auto existing = map_.load(pt_ns);
    if (existing.has_value() && existing->primary_received) {
        return;  // duplicate primary on the same PT
    }
    Coverage c = existing.value_or(Coverage{});
    c.primary_received = true;
    map_.store(pt_ns, c);
    ++primary_received_;
}

auto RedundantRxTracker::on_redundant(int64_t pt_ns) noexcept -> bool
{
    initialize_cursor_if_needed(pt_ns);
    auto existing = map_.load(pt_ns);
    Coverage c = existing.value_or(Coverage{});
    if (c.redundant_received) {
        return false;  // duplicate redundant
    }
    bool const rescue = !c.primary_received;
    c.redundant_received = true;
    map_.store(pt_ns, c);
    ++redundant_received_;
    return rescue;
}

void RedundantRxTracker::retire_slot(int64_t pt_ns) noexcept
{
    auto cov = map_.load(pt_ns);
    if (!cov.has_value()) {
        ++true_loss_;
        return;
    }
    if (!cov->primary_received && cov->redundant_received) {
        ++recovered_;
    }
    // Primary-received slots (with or without redundant) were already
    // counted at receive time; just clear the slot here.
    map_.clear(pt_ns);
}

void RedundantRxTracker::scan(int64_t latest_pt_ns, int64_t grace_ns) noexcept
{
    if (!cursor_initialized_) {
        return;
    }
    int64_t const cutoff = latest_pt_ns - grace_ns;
    while (cursor_pt_ + slot_width_ns_ <= cutoff) {
        retire_slot(cursor_pt_);
        cursor_pt_ += slot_width_ns_;
    }
}

void RedundantRxTracker::flush_all(int64_t end_pt_ns) noexcept
{
    if (!cursor_initialized_) {
        return;
    }
    while (cursor_pt_ <= end_pt_ns) {
        retire_slot(cursor_pt_);
        cursor_pt_ += slot_width_ns_;
    }
}

}  // namespace statusbar::udptun
