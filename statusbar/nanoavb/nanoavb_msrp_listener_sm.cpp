// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/nanoavb/nanoavb_msrp_listener_sm.hpp"

#include "statusbar/sm/sm_core.hpp"

#include <functional>
#include <string>

namespace statusbar::nanoavb::msrp_listener_sm {

void init(Context& ctx, TimePoint time)
{
    ctx.last_action = function_name<&init>;
    ctx.reserved = false;
    ctx.failed = false;
    ctx.callbacks.init(ctx, time);
}

void mark_ready(Context& ctx, TimePoint time)
{
    ctx.last_action = function_name<&mark_ready>;
    ctx.reserved = true;
    ctx.failed = false;
    ctx.callbacks.mark_ready(ctx, time);
}

void mark_failed(Context& ctx, TimePoint time)
{
    ctx.last_action = function_name<&mark_failed>;
    ctx.reserved = false;
    ctx.failed = true;
    ctx.callbacks.mark_failed(ctx, time);
}

void msrp_listener_leave(Context& ctx, TimePoint time)
{
    ctx.last_action = function_name<&msrp_listener_leave>; /* withdraw Listener attr */
    ctx.reserved = false;
    ctx.callbacks.msrp_listener_leave(ctx, time);
}

void mark_idle(Context& ctx, TimePoint time)
{
    ctx.last_action = function_name<&mark_idle>;
    ctx.reserved = false;
    ctx.failed = false;
    ctx.callbacks.mark_idle(ctx, time);
}

}  // namespace statusbar::nanoavb::msrp_listener_sm
