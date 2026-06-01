// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_tx_history.hpp"

#include "statusbar/test/test.hpp"

using namespace statusbar;

TEST(udptun_tx_history, empty_returns_zero)
{
    udptun::TxHistory h;
    EXPECT_TRUE(h.empty());
    EXPECT_EQ(h.sent_count_at_or_before(0), uint64_t{0});
    EXPECT_EQ(h.sent_count_at_or_before(1'000'000'000), uint64_t{0});
}

TEST(udptun_tx_history, lookup_finds_entry_at_or_before_cutoff)
{
    udptun::TxHistory h;
    h.record(100, 1);
    h.record(200, 2);
    h.record(300, 3);
    h.record(400, 4);

    EXPECT_EQ(h.sent_count_at_or_before(300), uint64_t{3});
    EXPECT_EQ(h.sent_count_at_or_before(350), uint64_t{3});
    EXPECT_EQ(h.sent_count_at_or_before(10'000), uint64_t{4});
    EXPECT_EQ(h.sent_count_at_or_before(50), uint64_t{0});
}

TEST(udptun_tx_history, ring_wraps_and_drops_oldest)
{
    udptun::TxHistory h;
    for (size_t i = 0; i < udptun::TxHistory::capacity + 100; ++i) {
        h.record(static_cast<int64_t>(i + 1) * 1000, static_cast<uint64_t>(i + 1));
    }
    EXPECT_EQ(h.size(), udptun::TxHistory::capacity);

    auto const last_ts = static_cast<int64_t>(udptun::TxHistory::capacity + 100) * 1000;
    EXPECT_EQ(h.sent_count_at_or_before(last_ts), uint64_t{udptun::TxHistory::capacity + 100});

    EXPECT_EQ(h.sent_count_at_or_before(0), uint64_t{0});
}

TEST_MAIN(statusbar_udptun, udptun_tx_history_test)
