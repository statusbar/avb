// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/nanoavb/nanoavb_acmp_listener_sm.hpp"

#include "statusbar/sm/sm_core.hpp"

#include <functional>
#include <string>

namespace statusbar::nanoavb::acmp_listener_sm {

void init(Context& ctx, TimePoint time)
{
    ctx.last_action = function_name<&init>;
    ctx.connected = false;
    ctx.callbacks.init(ctx, time);
}

void mark_connected(Context& ctx, TimePoint time)
{
    ctx.last_action = function_name<&mark_connected>;
    ctx.connected = true;
    ctx.callbacks.mark_connected(ctx, time);
}

void mark_failed(Context& ctx, TimePoint time)
{
    ctx.last_action = function_name<&mark_failed>;
    ctx.connected = false;
    ctx.callbacks.mark_failed(ctx, time);
}

void mark_disconnected(Context& ctx, TimePoint time)
{
    ctx.last_action = function_name<&mark_disconnected>;
    ctx.connected = false;
    ctx.callbacks.mark_disconnected(ctx, time);
}

}  // namespace statusbar::nanoavb::acmp_listener_sm
