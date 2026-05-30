// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/srp/srp_mrp_applicant_sm.hpp"

#include "statusbar/test/test.hpp"

#include <cstdint>

using namespace statusbar;
using namespace statusbar::srp::mrp::applicant_sm;

namespace {

// Shortcut: run a series of events to walk the state machine into the
// target state. Returns the Machine after the walk.
struct Walker
{
    Machine machine{};
    Context ctx{};

    // Fires one event and records the outputs in ctx.
    void fire(Def::Event event)
    {
        ctx.clear_outputs();
        machine.handle_event(ctx, event);
    }
};

}  // namespace

//
// Initialization
//

TEST(mrp_applicant_sm, initial_state_is_start_then_uct_to_vo)
{
    Walker w{};
    // Before any event, state is Start. After first handle_event(), UCT
    // fires and we land in Vo before the actual event is processed.
    EXPECT_EQ(static_cast<int>(w.machine.current_state()), static_cast<int>(Def::State::Start));
    // Firing Leave from Start: UCT runs first -> Vo, then Leave from
    // Vo has no table entry so state stays Vo.
    w.fire(Def::Event::Leave);
    EXPECT_EQ(static_cast<int>(w.machine.current_state()), static_cast<int>(Def::State::Vo));
}

//
// The critical Applicant An + Tx cell (Clause 10.7.7 Note 8)
//

TEST(mrp_applicant_sm, tx_from_an_with_registrar_in_goes_to_qa)
{
    // Table direct lookup avoids the need to walk the FSM into An.
    auto const& t = table.get(Def::State::An, Def::Event::TxRegistrarIn);
    EXPECT_TRUE(t.is_valid());
    EXPECT_EQ(static_cast<int>(t.next_state), static_cast<int>(Def::State::Qa));
}

TEST(mrp_applicant_sm, tx_from_an_with_registrar_mt_goes_to_aa)
{
    auto const& t = table.get(Def::State::An, Def::Event::TxRegistrarMt);
    EXPECT_TRUE(t.is_valid());
    EXPECT_EQ(static_cast<int>(t.next_state), static_cast<int>(Def::State::Aa));
}

//
// TX events across all states — spot check that transitions match
// mrp.c:719-791.
//

TEST(mrp_applicant_sm, tx_vn_sends_new_goes_to_an)
{
    auto const& t = table.get(Def::State::Vn, Def::Event::TxRegistrarIn);
    EXPECT_TRUE(t.is_valid());
    EXPECT_EQ(static_cast<int>(t.next_state), static_cast<int>(Def::State::An));
}

TEST(mrp_applicant_sm, tx_la_sends_leave_goes_to_vo)
{
    auto const& t = table.get(Def::State::La, Def::Event::TxRegistrarIn);
    EXPECT_TRUE(t.is_valid());
    EXPECT_EQ(static_cast<int>(t.next_state), static_cast<int>(Def::State::Vo));
}

TEST(mrp_applicant_sm, tx_qa_self_loop)
{
    auto const& t = table.get(Def::State::Qa, Def::Event::TxRegistrarIn);
    EXPECT_TRUE(t.is_valid());
    EXPECT_EQ(static_cast<int>(t.next_state), static_cast<int>(Def::State::Qa));
}

//
// Action side effects — fire a TX event from a state that produces a
// known output and verify the Context is populated correctly.
//

TEST(mrp_applicant_sm, tx_from_vn_sets_send_new_required)
{
    Walker w{};
    w.fire(Def::Event::UCT);  // Start -> Vo
    w.fire(Def::Event::New);  // Vo -> Vn
    EXPECT_EQ(static_cast<int>(w.machine.current_state()), static_cast<int>(Def::State::Vn));
    w.fire(Def::Event::TxRegistrarMt);
    EXPECT_EQ(static_cast<int>(w.machine.current_state()), static_cast<int>(Def::State::An));
    EXPECT_TRUE(w.ctx.tx_pending);
    EXPECT_EQ(static_cast<int>(w.ctx.send_msg), static_cast<int>(SendMessage::New));
    EXPECT_EQ(static_cast<int>(w.ctx.encode), static_cast<int>(Encoding::Required));
}

TEST(mrp_applicant_sm, tx_from_vo_sets_in_optional)
{
    Walker w{};
    w.fire(Def::Event::UCT);  // Start -> Vo
    w.fire(Def::Event::TxRegistrarMt);
    EXPECT_EQ(static_cast<int>(w.machine.current_state()), static_cast<int>(Def::State::Vo));  // self-loop
    EXPECT_TRUE(w.ctx.tx_pending);
    EXPECT_EQ(static_cast<int>(w.ctx.send_msg), static_cast<int>(SendMessage::In));
    EXPECT_EQ(static_cast<int>(w.ctx.encode), static_cast<int>(Encoding::Optional));
}

//
// p2pmac=true collapses rJoinIn on Vo/Vp to no-op.
//

TEST(mrp_applicant_sm, rjoinin_from_vo_does_nothing_under_p2pmac)
{
    auto const& t = table.get(Def::State::Vo, Def::Event::RJoinIn);
    EXPECT_FALSE(t.is_valid());  // no table entry = no-op
}

TEST(mrp_applicant_sm, rjoinin_from_vp_does_nothing_under_p2pmac)
{
    auto const& t = table.get(Def::State::Vp, Def::Event::RJoinIn);
    EXPECT_FALSE(t.is_valid());
}

TEST(mrp_applicant_sm, rjoinin_from_aa_goes_to_qa)
{
    auto const& t = table.get(Def::State::Aa, Def::Event::RJoinIn);
    EXPECT_TRUE(t.is_valid());
    EXPECT_EQ(static_cast<int>(t.next_state), static_cast<int>(Def::State::Qa));
}

//
// rIn under p2pmac=true: Aa -> Qa unconditional
//

TEST(mrp_applicant_sm, rin_from_aa_goes_to_qa)
{
    auto const& t = table.get(Def::State::Aa, Def::Event::RIn);
    EXPECT_TRUE(t.is_valid());
    EXPECT_EQ(static_cast<int>(t.next_state), static_cast<int>(Def::State::Qa));
}

//
// Periodic: Qa -> Aa, Qp -> Ap
//

TEST(mrp_applicant_sm, periodic_qa_to_aa)
{
    auto const& t = table.get(Def::State::Qa, Def::Event::Periodic);
    EXPECT_TRUE(t.is_valid());
    EXPECT_EQ(static_cast<int>(t.next_state), static_cast<int>(Def::State::Aa));
}

TEST(mrp_applicant_sm, periodic_qp_to_ap)
{
    auto const& t = table.get(Def::State::Qp, Def::Event::Periodic);
    EXPECT_TRUE(t.is_valid());
    EXPECT_EQ(static_cast<int>(t.next_state), static_cast<int>(Def::State::Ap));
}

TEST(mrp_applicant_sm, periodic_on_other_states_is_noop)
{
    // Vo, Vp, Vn, An, Aa, La, Ao, Qo, Ap, Lo: no transition
    EXPECT_FALSE(table.get(Def::State::Vo, Def::Event::Periodic).is_valid());
    EXPECT_FALSE(table.get(Def::State::Aa, Def::Event::Periodic).is_valid());
    EXPECT_FALSE(table.get(Def::State::Lo, Def::Event::Periodic).is_valid());
}

//
// New event: all non-{Vn,An} states go to Vn
//

TEST(mrp_applicant_sm, new_event_promotes_to_vn)
{
    EXPECT_EQ(static_cast<int>(table.get(Def::State::Vo, Def::Event::New).next_state), static_cast<int>(Def::State::Vn));
    EXPECT_EQ(static_cast<int>(table.get(Def::State::Qa, Def::Event::New).next_state), static_cast<int>(Def::State::Vn));
    EXPECT_EQ(static_cast<int>(table.get(Def::State::Lo, Def::Event::New).next_state), static_cast<int>(Def::State::Vn));
    EXPECT_FALSE(table.get(Def::State::Vn, Def::Event::New).is_valid());  // stays
    EXPECT_FALSE(table.get(Def::State::An, Def::Event::New).is_valid());  // stays
}

//
// Lv event: Vn/An/Aa/Qa -> La; Vp -> Vo; Ap -> Ao; Qp -> Qo
//

TEST(mrp_applicant_sm, leave_event_transitions)
{
    EXPECT_EQ(static_cast<int>(table.get(Def::State::Vn, Def::Event::Leave).next_state), static_cast<int>(Def::State::La));
    EXPECT_EQ(static_cast<int>(table.get(Def::State::An, Def::Event::Leave).next_state), static_cast<int>(Def::State::La));
    EXPECT_EQ(static_cast<int>(table.get(Def::State::Aa, Def::Event::Leave).next_state), static_cast<int>(Def::State::La));
    EXPECT_EQ(static_cast<int>(table.get(Def::State::Qa, Def::Event::Leave).next_state), static_cast<int>(Def::State::La));
    EXPECT_EQ(static_cast<int>(table.get(Def::State::Vp, Def::Event::Leave).next_state), static_cast<int>(Def::State::Vo));
    EXPECT_EQ(static_cast<int>(table.get(Def::State::Ap, Def::Event::Leave).next_state), static_cast<int>(Def::State::Ao));
    EXPECT_EQ(static_cast<int>(table.get(Def::State::Qp, Def::Event::Leave).next_state), static_cast<int>(Def::State::Qo));
}

//
// rLv/rLA/Redeclare: all share the same state transitions
//

TEST(mrp_applicant_sm, rleave_events_share_transitions)
{
    for (auto const ev : {Def::Event::RLeave, Def::Event::RLeaveAll, Def::Event::Redeclare}) {
        EXPECT_EQ(static_cast<int>(table.get(Def::State::Vo, ev).next_state), static_cast<int>(Def::State::Lo));
        EXPECT_EQ(static_cast<int>(table.get(Def::State::An, ev).next_state), static_cast<int>(Def::State::Vn));
        EXPECT_EQ(static_cast<int>(table.get(Def::State::Qa, ev).next_state), static_cast<int>(Def::State::Vp));
    }
}

//
// rNew is a no-op in the Applicant FSM (Registrar handles it)
//

TEST(mrp_applicant_sm, rnew_is_applicant_noop)
{
    // All cells for RNew are invalid (no transition)
    EXPECT_FALSE(table.get(Def::State::Vo, Def::Event::RNew).is_valid());
    EXPECT_FALSE(table.get(Def::State::Aa, Def::Event::RNew).is_valid());
    EXPECT_FALSE(table.get(Def::State::Qa, Def::Event::RNew).is_valid());
}

//
// Full state walk: local declare + tx round
//

TEST(mrp_applicant_sm, local_declare_new_walks_vo_vn_an_qa)
{
    Walker w{};
    w.fire(Def::Event::UCT);  // Start -> Vo
    EXPECT_EQ(static_cast<int>(w.machine.current_state()), static_cast<int>(Def::State::Vo));
    w.fire(Def::Event::New);  // Vo -> Vn (New declare)
    EXPECT_EQ(static_cast<int>(w.machine.current_state()), static_cast<int>(Def::State::Vn));
    w.fire(Def::Event::TxRegistrarMt);  // Vn -> An (send NEW)
    EXPECT_EQ(static_cast<int>(w.machine.current_state()), static_cast<int>(Def::State::An));
    EXPECT_TRUE(w.ctx.tx_pending);
    EXPECT_EQ(static_cast<int>(w.ctx.send_msg), static_cast<int>(SendMessage::New));

    // Next TX from An with registrar Mt -> Aa (still send NEW)
    w.fire(Def::Event::TxRegistrarMt);
    EXPECT_EQ(static_cast<int>(w.machine.current_state()), static_cast<int>(Def::State::Aa));
    EXPECT_TRUE(w.ctx.tx_pending);

    // After peer ack'd (rJoinIn), Aa -> Qa (quieted)
    w.fire(Def::Event::RJoinIn);
    EXPECT_EQ(static_cast<int>(w.machine.current_state()), static_cast<int>(Def::State::Qa));
}

TEST_MAIN(statusbar_srp, srp_mrp_applicant_sm_test)
