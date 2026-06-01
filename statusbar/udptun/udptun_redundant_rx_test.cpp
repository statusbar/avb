// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_redundant_rx.hpp"

#include "statusbar/test/test.hpp"

#include <cstdint>

using namespace statusbar;

namespace {

constexpr int64_t slot_ns = 1'000'000;  // 1 ms (= sender's tx_interval)

}  // namespace

TEST(udptun_redundant_rx, primary_arrivals_count_received)
{
    udptun::RedundantRxTracker t{slot_ns};
    t.on_primary(10'000'000);  // PT = 10 ms
    t.on_primary(11'000'000);
    t.on_primary(12'000'000);
    EXPECT_EQ(t.primary_received(), uint64_t{3});
    EXPECT_EQ(t.recovered(), uint64_t{0});
    EXPECT_EQ(t.true_loss(), uint64_t{0});
    EXPECT_EQ(t.pending_count(), size_t{3});
}

TEST(udptun_redundant_rx, primary_then_redundant_for_same_pt_no_rescue)
{
    udptun::RedundantRxTracker t{slot_ns};
    t.on_primary(5'000'000);
    EXPECT_FALSE(t.on_redundant(5'000'000));  // primary already won
    EXPECT_EQ(t.primary_received(), uint64_t{1});
    EXPECT_EQ(t.redundant_received(), uint64_t{1});
    EXPECT_EQ(t.recovered(), uint64_t{0});
    EXPECT_EQ(t.true_loss(), uint64_t{0});
}

TEST(udptun_redundant_rx, redundant_only_at_pt_counts_recovered_after_scan)
{
    udptun::RedundantRxTracker t{slot_ns};
    EXPECT_TRUE(t.on_redundant(10'000'000));
    // latest=11ms past 10ms by exactly slot_width with grace=0 →
    // cursor retires slot 10 (the only one).
    t.scan(/*latest_pt*/ 11'000'000, /*grace*/ 0);
    EXPECT_EQ(t.recovered(), uint64_t{1});
    EXPECT_EQ(t.true_loss(), uint64_t{0});
}

TEST(udptun_redundant_rx, gap_with_no_redundant_counts_as_true_loss)
{
    udptun::RedundantRxTracker t{slot_ns};
    t.on_primary(10'000'000);
    t.on_primary(12'000'000);  // 11 ms slot is empty
    // latest=12ms is the highest PT we've observed; cursor retires
    // slots whose PT is at most 12-slot_width=11ms behind latest.
    t.scan(12'000'000, 0);
    EXPECT_EQ(t.true_loss(), uint64_t{1});  // 11 ms slot
    EXPECT_EQ(t.primary_received(), uint64_t{2});
    // The 12 ms slot is still pending — within grace window.
    EXPECT_EQ(t.pending_count(), size_t{1});
}

TEST(udptun_redundant_rx, redundant_before_primary_at_same_pt_primary_wins)
{
    udptun::RedundantRxTracker t{slot_ns};
    EXPECT_TRUE(t.on_redundant(10'000'000));  // first arrival → rescue at receive
    t.on_primary(10'000'000);                 // primary catches up; flips slot
    t.scan(11'000'000, 0);
    EXPECT_EQ(t.primary_received(), uint64_t{1});
    EXPECT_EQ(t.redundant_received(), uint64_t{1});
    EXPECT_EQ(t.recovered(), uint64_t{0});  // primary won at deadline
    EXPECT_EQ(t.true_loss(), uint64_t{0});
}

TEST(udptun_redundant_rx, redundant_only_no_primary_counts_recovered)
{
    udptun::RedundantRxTracker t{slot_ns};
    EXPECT_TRUE(t.on_redundant(20'000'000));
    t.scan(21'000'000, 0);
    EXPECT_EQ(t.recovered(), uint64_t{1});
    EXPECT_EQ(t.true_loss(), uint64_t{0});
}

TEST(udptun_redundant_rx, scan_respects_grace_window)
{
    udptun::RedundantRxTracker t{slot_ns};
    t.on_primary(10'000'000);
    t.on_primary(12'000'000);  // gap at 11 ms
    // grace=5ms with latest=12ms → cutoff=7ms; cursor at 10ms can't retire.
    t.scan(12'000'000, 5'000'000);
    EXPECT_EQ(t.true_loss(), uint64_t{0});
    EXPECT_EQ(t.pending_count(), size_t{2});
    // grace=0 with latest=13ms → cutoff=13ms; cursor walks 10, 11, 12.
    t.scan(13'000'000, 0);
    EXPECT_EQ(t.true_loss(), uint64_t{1});  // 11 ms slot
    EXPECT_EQ(t.primary_received(), uint64_t{2});
}

TEST(udptun_redundant_rx, flush_all_finalizes_remaining_slots)
{
    udptun::RedundantRxTracker t{slot_ns};
    t.on_primary(10'000'000);
    t.on_primary(13'000'000);                 // 11, 12 missing
    EXPECT_TRUE(t.on_redundant(12'000'000));  // 12 rescued
    t.flush_all(/*end_pt*/ 13'000'000);
    EXPECT_EQ(t.recovered(), uint64_t{1});  // 12
    EXPECT_EQ(t.true_loss(), uint64_t{1});  // 11
    EXPECT_EQ(t.primary_received(), uint64_t{2});
}

TEST(udptun_redundant_rx, flush_all_with_no_observations_is_noop)
{
    udptun::RedundantRxTracker t{slot_ns};
    t.flush_all(100'000'000);
    EXPECT_EQ(t.true_loss(), uint64_t{0});
    EXPECT_EQ(t.recovered(), uint64_t{0});
    EXPECT_EQ(t.primary_received(), uint64_t{0});
}

TEST(udptun_redundant_rx, duplicate_primary_does_not_double_count)
{
    udptun::RedundantRxTracker t{slot_ns};
    t.on_primary(10'000'000);
    t.on_primary(10'000'000);
    EXPECT_EQ(t.primary_received(), uint64_t{1});
}

TEST(udptun_redundant_rx, duplicate_redundant_does_not_double_rescue)
{
    udptun::RedundantRxTracker t{slot_ns};
    EXPECT_TRUE(t.on_redundant(10'000'000));
    EXPECT_FALSE(t.on_redundant(10'000'000));
    EXPECT_EQ(t.redundant_received(), uint64_t{1});
}

TEST(udptun_redundant_rx, scan_with_no_observations_is_noop)
{
    udptun::RedundantRxTracker t{slot_ns};
    t.scan(100'000'000, 1'000'000);
    EXPECT_EQ(t.true_loss(), uint64_t{0});
    EXPECT_EQ(t.recovered(), uint64_t{0});
    EXPECT_EQ(t.primary_received(), uint64_t{0});
}

TEST(udptun_redundant_rx, repeat_scan_does_not_re_retire)
{
    udptun::RedundantRxTracker t{slot_ns};
    t.on_primary(10'000'000);
    t.on_primary(12'000'000);  // gap at 11
    t.scan(15'000'000, 0);
    auto const tl1 = t.true_loss();
    auto const pr1 = t.primary_received();
    t.scan(15'000'000, 0);  // cursor already past everything
    EXPECT_EQ(t.true_loss(), tl1);
    EXPECT_EQ(t.primary_received(), pr1);
}

TEST(udptun_redundant_rx, mid_slot_pt_quantizes_to_slot_boundary)
{
    // Two PTs in [10ms, 11ms) land in the same slot (jitter <
    // slot_width). Second on_primary is treated as a duplicate.
    udptun::RedundantRxTracker t{slot_ns};
    t.on_primary(10'250'000);  // mid-slot
    t.on_primary(10'500'000);  // same slot
    EXPECT_EQ(t.primary_received(), uint64_t{1});
}

TEST(udptun_redundant_rx, large_gap_retires_each_missing_slot)
{
    udptun::RedundantRxTracker t{slot_ns};
    t.on_primary(10'000'000);
    t.on_primary(15'000'000);  // 11..14 missing (4 slots)
    t.scan(15'000'000, 0);
    EXPECT_EQ(t.true_loss(), uint64_t{4});
    EXPECT_EQ(t.primary_received(), uint64_t{2});
}

TEST(udptun_redundant_rx, redundant_filled_in_gap_then_primary_arrives_late)
{
    // Realistic ST 2022-7 sequence: redundant arrives in a gap, then
    // late primary catches up before deadline → primary wins, not
    // counted as recovered.
    udptun::RedundantRxTracker t{slot_ns};
    t.on_primary(10'000'000);
    t.on_primary(12'000'000);                 // 11 missing for now
    EXPECT_TRUE(t.on_redundant(11'000'000));  // rescues
    t.on_primary(11'000'000);                 // late primary arrives
    t.scan(13'000'000, 0);
    EXPECT_EQ(t.primary_received(), uint64_t{3});
    EXPECT_EQ(t.recovered(), uint64_t{0});
    EXPECT_EQ(t.true_loss(), uint64_t{0});
}

TEST_MAIN(statusbar_udptun, udptun_redundant_rx_test)
