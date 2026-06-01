// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_monotonic_ring.hpp"

#include "statusbar/test/test.hpp"

#include <cstdint>

using namespace statusbar;

namespace {

/// Small ring (capacity 4) so wrap behavior is easy to reach in tests.
using SmallRing = udptun::MonotonicRing<uint32_t, 4>;

}  // namespace

TEST(udptun_monotonic_ring, default_state_is_empty)
{
    SmallRing r;
    EXPECT_TRUE(r.empty());
    EXPECT_EQ(r.size(), size_t{0});
}

TEST(udptun_monotonic_ring, lookup_on_empty_ring_returns_nullopt)
{
    SmallRing r;
    EXPECT_FALSE(r.entry_at_or_before(0).has_value());
    EXPECT_FALSE(r.entry_at_or_before(1'000'000'000).has_value());
}

TEST(udptun_monotonic_ring, single_entry_lookup_boundaries)
{
    SmallRing r;
    r.record(100, 7U);
    EXPECT_FALSE(r.empty());
    EXPECT_EQ(r.size(), size_t{1});

    // Target before the only entry → no match (nothing recorded that early).
    EXPECT_FALSE(r.entry_at_or_before(99).has_value());

    // Target equal to entry timestamp → match.
    auto e = r.entry_at_or_before(100);
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ(e->timestamp_ns, int64_t{100});
    EXPECT_EQ(e->payload, uint32_t{7});

    // Target after the entry → still match (largest <= target).
    e = r.entry_at_or_before(500);
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ(e->timestamp_ns, int64_t{100});
}

TEST(udptun_monotonic_ring, multiple_entries_returns_largest_le_target)
{
    SmallRing r;
    r.record(100, 1U);
    r.record(200, 2U);
    r.record(300, 3U);
    EXPECT_EQ(r.size(), size_t{3});

    // Exact-match returns the matching entry.
    auto e = r.entry_at_or_before(200);
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ(e->payload, uint32_t{2});

    // Between two entries returns the older one.
    e = r.entry_at_or_before(250);
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ(e->payload, uint32_t{2});

    // Past the newest still returns the newest.
    e = r.entry_at_or_before(99'999);
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ(e->payload, uint32_t{3});

    // Before the oldest returns nullopt.
    EXPECT_FALSE(r.entry_at_or_before(99).has_value());
}

TEST(udptun_monotonic_ring, fills_to_capacity_size_caps)
{
    SmallRing r;
    for (uint32_t i = 0; i < SmallRing::capacity; ++i) {
        r.record(static_cast<int64_t>(i + 1) * 100, i);
    }
    EXPECT_EQ(r.size(), SmallRing::capacity);

    // Add one more; size still capped at capacity.
    r.record(500, 99);
    EXPECT_EQ(r.size(), SmallRing::capacity);
}

TEST(udptun_monotonic_ring, wrap_evicts_oldest)
{
    SmallRing r;
    // Fill: timestamps 100, 200, 300, 400.
    r.record(100, 1U);
    r.record(200, 2U);
    r.record(300, 3U);
    r.record(400, 4U);
    // Capacity reached. Newest=400, oldest=100.

    // One more: timestamps now 200, 300, 400, 500 (100 dropped).
    r.record(500, 5U);
    EXPECT_EQ(r.size(), SmallRing::capacity);

    // Lookup at 100 must NOT find anything: 100 has aged out.
    EXPECT_FALSE(r.entry_at_or_before(100).has_value());
    // Likewise anything earlier than the new oldest (200).
    EXPECT_FALSE(r.entry_at_or_before(199).has_value());

    // Lookup at 200 finds the new oldest.
    auto e = r.entry_at_or_before(200);
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ(e->payload, uint32_t{2});

    // Lookup past newest finds the newest.
    e = r.entry_at_or_before(99'999);
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ(e->payload, uint32_t{5});
}

TEST(udptun_monotonic_ring, large_wrap_keeps_only_recent_window)
{
    // Push many more entries than capacity; only the last capacity remain.
    SmallRing r;
    for (uint32_t i = 1; i <= 100; ++i) {
        r.record(static_cast<int64_t>(i) * 10, i);
    }
    EXPECT_EQ(r.size(), SmallRing::capacity);

    // Most-recent entry: payload 100 at timestamp 1000.
    auto e = r.entry_at_or_before(99'999);
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ(e->payload, uint32_t{100});

    // Oldest in-ring: payload 97 at timestamp 970.
    e = r.entry_at_or_before(970);
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ(e->payload, uint32_t{97});

    // Target below the oldest in-ring → nullopt.
    EXPECT_FALSE(r.entry_at_or_before(969).has_value());
}

TEST(udptun_monotonic_ring, equal_timestamps_returns_newest_entry)
{
    // Two records with the same timestamp: backward scan from newest
    // returns the more recently recorded one.
    SmallRing r;
    r.record(100, 1U);
    r.record(100, 2U);
    auto e = r.entry_at_or_before(100);
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ(e->payload, uint32_t{2});
}

TEST(udptun_monotonic_ring, capacity_constant_matches_template_arg)
{
    static_assert(SmallRing::capacity == 4);
    using LargeRing = udptun::MonotonicRing<int, 1024>;
    static_assert(LargeRing::capacity == 1024);
}

TEST_MAIN(statusbar_udptun, udptun_monotonic_ring_test)
