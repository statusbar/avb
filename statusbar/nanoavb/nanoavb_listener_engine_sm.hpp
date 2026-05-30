#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/sg14/inplace_function.h"
#include "statusbar/sm/sm.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace statusbar::nanoavb::listener_engine_sm {

using namespace statusbar::sm;

struct Context;

struct Callbacks
{
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> init;
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> enable_rx_filter;
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> start_sync;
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> start_audio_sink;
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> stop_all;
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> resync;
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> mute_out;
    statusbar::sg14::inplace_function<void(Context& ctx, TimePoint), 64> unmute_out;
};

struct Context
{
    Callbacks callbacks{};
    bool play_allowed{false};  // computed gate
    std::string last_action{};
};

struct Def
{
    using Context = listener_engine_sm::Context;
    enum class State : uint8_t
    {
        Start = 0,
        Off,
        Listening,
        Syncing,
        Playing,
        Muted,
        Count
    };
    enum class Event : uint8_t
    {
        UCT = 0,
        GateListen,
        FirstPacket,
        Synced,
        GateStop,
        PacketGap,
        Underrun,
        Recovered,
        Count
    };
};

inline void init(Context& ctx, TimePoint time)
{
    ctx.last_action = "init";
    ctx.callbacks.init(ctx, time);
}

inline void enable_rx_filter(Context& ctx, TimePoint time)
{
    ctx.last_action = "enable_rx_filter"; /* program filters */
    ctx.callbacks.enable_rx_filter(ctx, time);
}

inline void start_sync(Context& ctx, TimePoint time)
{
    ctx.last_action = "start_sync"; /* begin dejitter fill */
    ctx.callbacks.start_sync(ctx, time);
}

inline void start_audio_sink(Context& ctx, TimePoint time)
{
    ctx.last_action = "start_audio_sink"; /* open output */
    ctx.callbacks.start_audio_sink(ctx, time);
}

inline void stop_all(Context& ctx, TimePoint time)
{
    ctx.last_action = "stop_all"; /* stop rx + sink */
    ctx.callbacks.stop_all(ctx, time);
}

inline void resync(Context& ctx, TimePoint time)
{
    ctx.last_action = "resync"; /* drop buffer, reacquire */
    ctx.callbacks.resync(ctx, time);
}

inline void mute_out(Context& ctx, TimePoint time)
{
    ctx.last_action = "mute_out";
    ctx.callbacks.mute_out(ctx, time);
}

inline void unmute_out(Context& ctx, TimePoint time)
{
    ctx.last_action = "unmute_out";
    ctx.callbacks.unmute_out(ctx, time);
}

inline constexpr auto table = []() -> TransitionTable<Def> {
    using S = Def::State;
    using E = Def::Event;
    using T = Transitions<Def>;
    TransitionTable<Def> t{};

    t.at(S::Start, E::UCT) = T::action<init>(S::Off);

    t.at(S::Off, E::GateListen) = T::action<enable_rx_filter>(S::Listening);

    t.at(S::Listening, E::FirstPacket) = T::action<start_sync>(S::Syncing);
    t.at(S::Listening, E::GateStop) = T::action<stop_all>(S::Off);  // Allow stop from Listening

    t.at(S::Syncing, E::Synced) = T::action<start_audio_sink>(S::Playing);
    t.at(S::Syncing, E::GateStop) = T::action<stop_all>(S::Off);     // Allow stop from Syncing
    t.at(S::Syncing, E::PacketGap) = T::action<resync>(S::Syncing);  // Handle gap during sync

    t.at(S::Playing, E::GateStop) = T::action<stop_all>(S::Off);
    t.at(S::Playing, E::PacketGap) = T::action<resync>(S::Syncing);
    t.at(S::Playing, E::Underrun) = T::action<mute_out>(S::Muted);

    t.at(S::Muted, E::Recovered) = T::action<unmute_out>(S::Playing);
    t.at(S::Muted, E::GateStop) = T::action<stop_all>(S::Off);
    t.at(S::Muted, E::PacketGap) = T::action<resync>(S::Syncing);  // Handle gap while muted

    return t;
}();

using Machine = StateMachine<Def, table>;

}  // namespace statusbar::nanoavb::listener_engine_sm
