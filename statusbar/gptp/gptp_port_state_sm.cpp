// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/gptp/gptp_port_state_sm.hpp"

namespace statusbar::gptp::port_state_sm {

void a_init(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.as_capable = false;
    ctx.synced = false;
}

void a_enter_initializing(Context& /*ctx*/, TimePoint /*time*/) noexcept
{
    // no-op — state transition only
}

void a_enter_listening(Context& /*ctx*/, TimePoint /*time*/) noexcept
{
    // no-op — waiting for asCapable
}

void a_enter_uncalibrated(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.as_capable = true;
}

void a_enter_uncalibrated_with_pre_asc(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.as_capable = true;
}

void a_enter_slave(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.synced = true;
}

void a_enter_disabled(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.as_capable = false;
    ctx.synced = false;
}

void a_drop_as_capable(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.as_capable = false;
    ctx.synced = false;
}

void a_lose_sync(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.synced = false;
}

}  // namespace statusbar::gptp::port_state_sm
