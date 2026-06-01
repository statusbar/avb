// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/stun/stun_client_sm.hpp"

#include "statusbar/test/test.hpp"

using namespace statusbar::stun;

TEST(stun_client_sm, idle_to_registering_on_start)
{
    ClientStateMachine sm{};
    ClientContext ctx{};
    EXPECT_EQ(static_cast<int>(sm.current_state()), static_cast<int>(ClientDef::State::Idle));
    sm.handle_event(ctx, ClientDef::Event::Start);
    EXPECT_EQ(static_cast<int>(sm.current_state()), static_cast<int>(ClientDef::State::Registering));
    EXPECT_TRUE(ctx.emit_send);
    EXPECT_EQ(ctx.retransmits, 0);
}

TEST(stun_client_sm, rto_increments_retransmits)
{
    ClientStateMachine sm{};
    ClientContext ctx{};
    sm.handle_event(ctx, ClientDef::Event::Start);
    ctx.emit_send = false;
    sm.handle_event(ctx, ClientDef::Event::Rto);
    EXPECT_EQ(static_cast<int>(sm.current_state()), static_cast<int>(ClientDef::State::Registering));
    EXPECT_EQ(ctx.retransmits, 1);
    EXPECT_TRUE(ctx.emit_send);
}

TEST(stun_client_sm, retry_budget_exhausted_fails)
{
    ClientStateMachine sm{};
    ClientContext ctx{};
    sm.handle_event(ctx, ClientDef::Event::Start);
    sm.handle_event(ctx, ClientDef::Event::RetryBudgetExhausted);
    EXPECT_EQ(static_cast<int>(sm.current_state()), static_cast<int>(ClientDef::State::Failed));
    EXPECT_TRUE(ctx.emit_failure);
}

TEST(stun_client_sm, response_waiting_moves_to_waiting)
{
    ClientStateMachine sm{};
    ClientContext ctx{};
    sm.handle_event(ctx, ClientDef::Event::Start);
    sm.handle_event(ctx, ClientDef::Event::ResponseWaiting);
    EXPECT_EQ(static_cast<int>(sm.current_state()), static_cast<int>(ClientDef::State::Waiting));
    EXPECT_EQ(ctx.retransmits, 0);
}

TEST(stun_client_sm, refresh_tick_in_waiting_resends)
{
    ClientStateMachine sm{};
    ClientContext ctx{};
    sm.handle_event(ctx, ClientDef::Event::Start);
    sm.handle_event(ctx, ClientDef::Event::ResponseWaiting);
    ctx.emit_send = false;
    sm.handle_event(ctx, ClientDef::Event::RefreshTick);
    EXPECT_EQ(static_cast<int>(sm.current_state()), static_cast<int>(ClientDef::State::Registering));
    EXPECT_TRUE(ctx.emit_send);
}

TEST(stun_client_sm, response_paired_moves_to_paired)
{
    ClientStateMachine sm{};
    ClientContext ctx{};
    sm.handle_event(ctx, ClientDef::Event::Start);
    sm.handle_event(ctx, ClientDef::Event::ResponsePaired);
    EXPECT_EQ(static_cast<int>(sm.current_state()), static_cast<int>(ClientDef::State::Paired));
    EXPECT_TRUE(ctx.reached_pairing);
}

TEST(stun_client_sm, paired_refresh_loop)
{
    ClientStateMachine sm{};
    ClientContext ctx{};
    sm.handle_event(ctx, ClientDef::Event::Start);
    sm.handle_event(ctx, ClientDef::Event::ResponsePaired);
    ctx.emit_send = false;
    sm.handle_event(ctx, ClientDef::Event::RefreshTick);
    EXPECT_EQ(static_cast<int>(sm.current_state()), static_cast<int>(ClientDef::State::Registering));
    EXPECT_TRUE(ctx.emit_send);
    sm.handle_event(ctx, ClientDef::Event::ResponsePaired);
    EXPECT_EQ(static_cast<int>(sm.current_state()), static_cast<int>(ClientDef::State::Paired));
}

TEST_MAIN(statusbar_stun, stun_client_sm_test)
