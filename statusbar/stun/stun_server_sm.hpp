#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Server-side per-session state machine. Each entry in the server's
/// session table owns one of these. The Pollable feeds events derived
/// from incoming REGISTER messages and from time ticks; the actions
/// only update bookkeeping fields, since reply-emission is naturally
/// driven by the inbound request that triggered the event.

#include "statusbar/ieee/ieee_ethernet.hpp"
#include "statusbar/net/net_address.hpp"
#include "statusbar/sm/sm_core.hpp"
#include "statusbar/stun/stun_constants.hpp"

#include <cstdint>

namespace statusbar::stun {

struct ServerSessionEntry
{
    statusbar::ieee::Eui64 eui64{};
    statusbar::net::SocketAddress reflexive_addr{};
    int64_t last_refresh_ns{0};
    bool present{false};
};

struct ServerSessionContext
{
    int64_t now_ns{0};
    int64_t expiry_ns{0};
    int64_t last_event_ns{0};
    ServerSessionEntry first{};
    ServerSessionEntry second{};
};

struct ServerDef
{
    using Context = ServerSessionContext;

    enum class State : uint8_t
    {
        Empty = 0,
        OneRegistered,
        Paired,
        Expired,
        Count,
    };

    enum class Event : uint8_t
    {
        FirstRegister = 0,
        SecondRegister,
        RefreshOne,
        RefreshTwo,
        DuplicateEui64,  ///< Same EUI-64 re-registers from a different address; treat as Refresh
        ExpireTick,
        Count,
    };
};

namespace server_sm_actions {

inline void touch(ServerSessionContext& ctx, statusbar::sm::TimePoint /*tp*/)
{
    ctx.last_event_ns = ctx.now_ns;
}

inline void mark_expired(ServerSessionContext& ctx, statusbar::sm::TimePoint /*tp*/)
{
    ctx.first.present = false;
    ctx.second.present = false;
}

}  // namespace server_sm_actions

inline constexpr auto server_table = [] {
    using State = ServerDef::State;
    using Event = ServerDef::Event;
    using T = statusbar::sm::Transitions<ServerDef>;
    using namespace server_sm_actions;

    statusbar::sm::TransitionTable<ServerDef> t{};

    t.at(State::Empty, Event::FirstRegister) = T::action<touch>(State::OneRegistered);

    t.at(State::OneRegistered, Event::SecondRegister) = T::action<touch>(State::Paired);
    t.at(State::OneRegistered, Event::RefreshOne) = T::action<touch>(State::OneRegistered);
    t.at(State::OneRegistered, Event::DuplicateEui64) = T::action<touch>(State::OneRegistered);
    t.at(State::OneRegistered, Event::ExpireTick) = T::action<mark_expired>(State::Expired);

    t.at(State::Paired, Event::RefreshOne) = T::action<touch>(State::Paired);
    t.at(State::Paired, Event::RefreshTwo) = T::action<touch>(State::Paired);
    t.at(State::Paired, Event::DuplicateEui64) = T::action<touch>(State::Paired);
    t.at(State::Paired, Event::ExpireTick) = T::action<mark_expired>(State::Expired);

    return t;
}();

using ServerSessionStateMachine = statusbar::sm::StateMachine<ServerDef, server_table>;

}  // namespace statusbar::stun
