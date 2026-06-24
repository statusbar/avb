#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/sg14/inplace_function.h"
#include "statusbar/sm/sm.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace statusbar::nanoavb::acmp_talker_sm {

using namespace statusbar::sm;

struct Context;

struct Callbacks
{
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> init;
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> add_listener;
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> reject_connect;
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> remove_listener;
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> drop_all;
};

struct Context
{
    Callbacks callbacks{};
    uint32_t listener_count{0};
};

struct Def
{
    using Context = acmp_talker_sm::Context;
    enum class State : uint8_t
    {
        Start = 0,
        Idle,
        Connected,
        Count
    };
    enum class Event : uint8_t
    {
        UCT = 0,
        ConnectRxOk,
        ConnectRxFail,
        DisconnectRx,
        LinkDown,
        Count
    };
};

void init(Context& ctx, TimePoint time);
void add_listener(Context& ctx, TimePoint time);

inline void reject_connect(Context& ctx, TimePoint time)
{
    ctx.callbacks.reject_connect(ctx, time);
}

void remove_listener(Context& ctx, TimePoint time);
void drop_all(Context& ctx, TimePoint time);

inline constexpr auto table = []() -> TransitionTable<Def> {
    using S = Def::State;
    using E = Def::Event;
    using T = Transitions<Def>;
    TransitionTable<Def> t{};

    t.at(S::Start, E::UCT) = T::action<init>(S::Idle);

    t.at(S::Idle, E::ConnectRxOk) = T::action<add_listener>(S::Connected);
    t.at(S::Idle, E::ConnectRxFail) = T::action<reject_connect>(S::Idle);

    t.at(S::Connected, E::ConnectRxOk) = T::action<add_listener>(S::Connected);
    // Disconnect handling: you'll typically post different event based on count,
    // or do a self-transition then have your supervisor post another event.
    t.at(S::Connected, E::DisconnectRx) = T::action<remove_listener>(S::Connected);

    t.at(S::Connected, E::LinkDown) = T::action<drop_all>(S::Idle);

    return t;
}();

using Machine = StateMachine<Def, table>;

}  // namespace statusbar::nanoavb::acmp_talker_sm
