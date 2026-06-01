// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_redundant_tx.hpp"

#include "statusbar/test/test.hpp"

using namespace statusbar;

TEST(udptun_redundant_tx, empty_returns_nullopt)
{
    udptun::RedundantTxBuffer b;
    EXPECT_TRUE(b.empty());
    EXPECT_FALSE(b.entry_at_or_before(0).has_value());
    EXPECT_FALSE(b.entry_at_or_before(1'000'000'000).has_value());
}

TEST(udptun_redundant_tx, lookup_returns_most_recent_at_or_before)
{
    udptun::RedundantTxBuffer b;
    b.record(1000, 0, 100);
    b.record(2000, 1, 200);
    b.record(3000, 2, 300);
    b.record(4000, 3, 400);

    auto e = b.entry_at_or_before(3000);
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ(e->sequence, uint32_t{2});
    EXPECT_EQ(e->tx_gptp_ns, int64_t{300});

    e = b.entry_at_or_before(3500);
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ(e->sequence, uint32_t{2});

    e = b.entry_at_or_before(10'000);
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ(e->sequence, uint32_t{3});

    EXPECT_FALSE(b.entry_at_or_before(500).has_value());
}

TEST(udptun_redundant_tx, ring_wraps)
{
    udptun::RedundantTxBuffer b;
    for (size_t i = 0; i < udptun::RedundantTxBuffer::capacity + 50; ++i) {
        b.record(static_cast<int64_t>(i + 1) * 1000, static_cast<uint32_t>(i), static_cast<int64_t>(i) * 10);
    }
    EXPECT_EQ(b.size(), udptun::RedundantTxBuffer::capacity);
    auto last = b.entry_at_or_before(static_cast<int64_t>(udptun::RedundantTxBuffer::capacity + 50) * 1000);
    EXPECT_TRUE(last.has_value());
    EXPECT_EQ(last->sequence, uint32_t{udptun::RedundantTxBuffer::capacity + 49});
}

TEST_MAIN(statusbar_udptun, udptun_redundant_tx_test)
