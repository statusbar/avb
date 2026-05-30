#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/sg14/inplace_function.h"
#include "statusbar/sm/sm.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace statusbar::nanoavb::supervisor_sm {

using namespace statusbar::sm;

struct Context;

struct Callbacks
{
    statusbar::sg14::inplace_function<void(Context&, TimePoint), 64> init_iface{};
    statusbar::sg14::inplace_function<void(Context&, TimePoint), 64> start_protocols{};
    statusbar::sg14::inplace_function<void(Context&, TimePoint), 64> enter_wait_vlan{};
    statusbar::sg14::inplace_function<void(Context&, TimePoint), 64> enter_ready{};
    statusbar::sg14::inplace_function<void(Context&, TimePoint), 64> degrade_stop_streams{};
    statusbar::sg14::inplace_function<void(Context&, TimePoint), 64> stop_all{};
    statusbar::sg14::inplace_function<void(Context&, TimePoint), 64> timeout_gptp{};  // Called when gPTP lock times out
    statusbar::sg14::inplace_function<void(Context&, TimePoint), 64> timeout_vlan{};  // Called when VLAN registration times out
};

struct Context
{
    Callbacks callbacks{};
    // facts
    bool link_up{false};
    bool gptp_locked{false};
    bool vlan_base_ready{false};

    // diagnostics
    std::string last_action{};
};

struct Def
{
    using Context = supervisor_sm::Context;

    enum class State : uint8_t
    {
        Start = 0,
        Down,
        Init,
        WaitVlanBase,
        Ready,
        Degraded,
        Count
    };
    enum class Event : uint8_t
    {
        UCT = 0,
        LinkUp,
        LinkDown,
        GptpLocked,
        GptpLost,
        VlanBaseReady,
        Timeout,  // Watchdog timer expired in waiting states
        Count
    };
};

void init_iface(Context& ctx, TimePoint time);
void start_protocols(Context& ctx, TimePoint time);
void enter_wait_vlan(Context& ctx, TimePoint time);
void enter_ready(Context& ctx, TimePoint time);
void degrade_stop_streams(Context& ctx, TimePoint time);
void stop_all(Context& ctx, TimePoint time);
void timeout_gptp(Context& ctx, TimePoint time);
void timeout_vlan(Context& ctx, TimePoint time);

inline constexpr auto table = []() -> TransitionTable<Def> {
    using S = Def::State;
    using E = Def::Event;
    using T = Transitions<Def>;
    TransitionTable<Def> t{};

    t.at(S::Start, E::UCT) = T::action<init_iface>(S::Down);

    t.at(S::Down, E::LinkUp) = T::action<start_protocols>(S::Init);
    t.at(S::Down, E::LinkDown) = T::transition(S::Down);

    t.at(S::Init, E::GptpLocked) = T::action<enter_wait_vlan>(S::WaitVlanBase);
    t.at(S::Init, E::LinkDown) = T::action<stop_all>(S::Down);
    t.at(S::Init, E::Timeout) = T::action<timeout_gptp>(S::Down);  // gPTP lock timeout

    t.at(S::WaitVlanBase, E::VlanBaseReady) = T::action<enter_ready>(S::Ready);
    t.at(S::WaitVlanBase, E::GptpLost) = T::action<degrade_stop_streams>(S::Degraded);
    t.at(S::WaitVlanBase, E::LinkDown) = T::action<stop_all>(S::Down);
    t.at(S::WaitVlanBase, E::Timeout) = T::action<timeout_vlan>(S::Degraded);  // VLAN timeout

    t.at(S::Ready, E::GptpLost) = T::action<degrade_stop_streams>(S::Degraded);
    t.at(S::Ready, E::LinkDown) = T::action<stop_all>(S::Down);

    t.at(S::Degraded, E::GptpLocked) = T::action<enter_wait_vlan>(S::WaitVlanBase);
    t.at(S::Degraded, E::LinkDown) = T::action<stop_all>(S::Down);

    return t;
}();

using Machine = StateMachine<Def, table>;

}  // namespace statusbar::nanoavb::supervisor_sm
