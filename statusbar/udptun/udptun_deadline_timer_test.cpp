// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_deadline_timer.hpp"

#include "statusbar/test/test.hpp"

using namespace statusbar::udptun;

TEST(udptun_deadline_timer, default_constructed_is_disarmed)
{
    DeadlineTimer t;
    EXPECT_FALSE(t.armed());
    EXPECT_EQ(t.next_deadline_ns(), DeadlineTimer::disabled_value);
    EXPECT_EQ(t.consume_due(1'000'000'000), 0);
}

TEST(udptun_deadline_timer, arm_sets_first_deadline)
{
    DeadlineTimer t;
    t.arm(1'000'000'000, 100'000'000);
    EXPECT_TRUE(t.armed());
    EXPECT_EQ(t.next_deadline_ns(), int64_t{1'000'000'000});
}

TEST(udptun_deadline_timer, consume_due_below_deadline_is_noop)
{
    DeadlineTimer t;
    t.arm(1'000, 100);
    EXPECT_EQ(t.consume_due(999), 0);
    EXPECT_EQ(t.next_deadline_ns(), int64_t{1'000});
}

TEST(udptun_deadline_timer, consume_due_at_deadline_advances_one_tick)
{
    DeadlineTimer t;
    t.arm(1'000, 100);
    EXPECT_EQ(t.consume_due(1'000), 1);
    EXPECT_EQ(t.next_deadline_ns(), int64_t{1'100});
}

TEST(udptun_deadline_timer, consume_due_catches_up_missed_ticks)
{
    DeadlineTimer t;
    t.arm(1'000, 100);
    // We're 350 ns past the first deadline → 4 ticks should be consumed
    // and the next deadline should jump to 1'400 (= 1'000 + 4*100).
    EXPECT_EQ(t.consume_due(1'350), 4);
    EXPECT_EQ(t.next_deadline_ns(), int64_t{1'400});
}

TEST(udptun_deadline_timer, disarmed_arm_zero_interval)
{
    DeadlineTimer t;
    t.arm(1'000, 0);
    EXPECT_FALSE(t.armed());
    EXPECT_EQ(t.consume_due(2'000), 0);
}

TEST(udptun_deadline_timer, disarm_after_arm)
{
    DeadlineTimer t;
    t.arm(1'000, 100);
    t.disarm();
    EXPECT_FALSE(t.armed());
    EXPECT_EQ(t.consume_due(10'000), 0);
}

TEST(udptun_deadline_timer, poll_timeout_clamped_to_max)
{
    constexpr int64_t day_ns = 86'400'000'000'000LL;
    EXPECT_EQ(deadline_to_poll_timeout_ms(0, day_ns, 100), 100);
}

TEST(udptun_deadline_timer, poll_timeout_zero_when_due)
{
    EXPECT_EQ(deadline_to_poll_timeout_ms(/*now=*/2'000, /*deadline=*/1'500, /*max=*/100), 0);
}

TEST(udptun_deadline_timer, poll_timeout_disabled_returns_max)
{
    EXPECT_EQ(deadline_to_poll_timeout_ms(0, DeadlineTimer::disabled_value, 100), 100);
}

TEST(udptun_deadline_timer, poll_timeout_rounds_up_sub_millisecond)
{
    // 1 ns shy of 1 ms → must report at least 1 ms so we don't busy-spin.
    EXPECT_EQ(deadline_to_poll_timeout_ms(0, 999'999, 100), 1);
}

TEST_MAIN(statusbar_udptun, udptun_deadline_timer_test)
