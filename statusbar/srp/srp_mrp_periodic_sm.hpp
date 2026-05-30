#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

//
// IEEE 802.1Q-2014 Clause 10.7.5.23 — MRP Periodic Transmission
// ==============================================================
//
// Scope: local endpoint only. One instance per MRP participant (per
// port, per protocol) — NOT per attribute.
//
// The PeriodicTransmission FSM controls the 1 Hz periodic tick that
// drives the Applicant QA->AA and QP->AP re-advertisement transitions.
// It can be enabled and disabled at runtime.
//
// Reference: OpenAvnu mrpd mrp.c::mrp_periodictimer_fsm at mrp.c:509-545.
//
// Side effects — Context outputs:
//   timer_restart — set true when the driver must (re)arm the per-port
//                    Periodic timer (PeriodicTransmissionTime = 1 s).
//
// When the FSM enters the Passive state, no restart is requested; the
// currently-running timer (if any) simply expires without being
// rescheduled, exactly as in mrp.c.
//

#include "statusbar/sm/sm.hpp"

#include <cstdint>

namespace statusbar::srp::mrp::periodic_sm {

using namespace statusbar::sm;

struct Context
{
    // --- Transition side-effect outputs ---
    bool timer_restart{false};

    constexpr void clear_outputs() noexcept { timer_restart = false; }
};

struct Def
{
    using Context = periodic_sm::Context;

    /// Periodic FSM states — mrp.h:155-156 reused.
    enum class State : uint8_t
    {
        Start = 0,  ///< pseudo-state before BEGIN! (UCT -> Active)
        Passive,    ///< periodic transmission disabled
        Active,     ///< periodic transmission enabled and timer running
        Count,
    };

    /// Periodic FSM events.
    enum class Event : uint8_t
    {
        UCT = 0,          ///< unconditional — BEGIN! initialization from Start
        Periodic,         ///< periodic! — PeriodicTransmissionTime expired
        PeriodicEnable,   ///< enable the periodic transmission timer
        PeriodicDisable,  ///< disable the periodic transmission timer
        Count,
    };
};

//
// Action functions — defined in mrp_periodic_sm.cpp.
//

void a_init(Context& ctx, TimePoint time) noexcept;
void a_restart_timer(Context& ctx, TimePoint time) noexcept;

//
// Transition table — IEEE 802.1Q-2014 Clause 10.7.5.23.
// Reference: OpenAvnu mrpd mrp.c:509-545.
//
inline constexpr auto table = []() -> TransitionTable<Def> {
    using S = Def::State;
    using E = Def::Event;
    using T = Transitions<Def>;
    TransitionTable<Def> t{};

    // BEGIN! — mrp.c:519-521 — initialize Active and start timer.
    t.at(S::Start, E::UCT) = T::action<a_init>(S::Active);

    // periodic! — mrp.c:523-527 — rearm only if Active.
    t.at(S::Active, E::Periodic) = T::action<a_restart_timer>(S::Active);
    // Passive + Periodic: no entry = no-op (timer expires without restart)

    // enable! — mrp.c:532-536 — from Passive, become Active and start.
    t.at(S::Passive, E::PeriodicEnable) = T::action<a_restart_timer>(S::Active);
    // Active + PeriodicEnable: no entry (already Active)

    // disable! — mrp.c:528-531 — become Passive; let timer expire naturally.
    t.at(S::Active, E::PeriodicDisable) = T::transition(S::Passive);
    // Passive + PeriodicDisable: no entry (already Passive)

    return t;
}();

using Machine = StateMachine<Def, table>;

}  // namespace statusbar::srp::mrp::periodic_sm
