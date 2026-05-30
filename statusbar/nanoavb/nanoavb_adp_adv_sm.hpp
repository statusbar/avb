#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/sg14/inplace_function.h"
#include "statusbar/sm/sm.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace statusbar::nanoavb::adp_adv_sm {

using namespace statusbar::sm;

struct Context;

struct Callbacks
{
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> init;
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> start_adp;
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> stop_adp;
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> maybe_announce;
};

struct Context
{
    Callbacks callbacks{};
    bool enabled{false};
    std::string last_action{};
};

struct Def
{
    using Context = adp_adv_sm::Context;
    enum class State : uint8_t
    {
        Start = 0,
        Off,
        Advertising,
        Count
    };
    enum class Event : uint8_t
    {
        UCT = 0,
        Enable,
        Disable,
        Tick,
        Error,
        Count
    };
};

inline void init(Context& ctx, TimePoint time)
{
    ctx.last_action = "init";
    ctx.enabled = false;
}

inline void start_adp(Context& ctx, TimePoint time)
{
    ctx.last_action = "start_adp";
    ctx.enabled = true; /* adp_start() */
    ctx.callbacks.start_adp(ctx, time);
}

inline void stop_adp(Context& ctx, TimePoint time)
{
    ctx.last_action = "stop_adp";
    ctx.enabled = false; /* adp_stop() */
    ctx.callbacks.stop_adp(ctx, time);
}

inline void maybe_announce(Context& ctx, TimePoint time)
{
    ctx.last_action = "maybe_announce"; /* periodic */
    ctx.callbacks.maybe_announce(ctx, time);
}

inline constexpr auto table = []() -> TransitionTable<Def> {
    using S = Def::State;
    using E = Def::Event;
    using T = Transitions<Def>;
    TransitionTable<Def> t{};
    t.at(S::Start, E::UCT) = T::action<init>(S::Off);

    t.at(S::Off, E::Enable) = T::action<start_adp>(S::Advertising);

    t.at(S::Advertising, E::Disable) = T::action<stop_adp>(S::Off);
    t.at(S::Advertising, E::Tick) = T::action<maybe_announce>(S::Advertising);
    t.at(S::Advertising, E::Error) = T::action<stop_adp>(S::Off);

    return t;
}();

using Machine = StateMachine<Def, table>;

}  // namespace statusbar::nanoavb::adp_adv_sm
