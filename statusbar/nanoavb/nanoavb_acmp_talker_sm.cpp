// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/nanoavb/nanoavb_acmp_talker_sm.hpp"

#include "statusbar/sm/sm_core.hpp"

#include <functional>
#include <string>

namespace statusbar::nanoavb::acmp_talker_sm {

void init(Context& ctx, TimePoint time)
{
    ctx.last_action = "init";
    ctx.listener_count = 0;
    ctx.callbacks.init(ctx, time);
}

void add_listener(Context& ctx, TimePoint time)
{
    ctx.last_action = "add_listener";
    ctx.listener_count++;
    ctx.callbacks.add_listener(ctx, time);
}

void remove_listener(Context& ctx, TimePoint time)
{
    ctx.last_action = "remove_listener";
    if (ctx.listener_count > 0) {
        ctx.listener_count--;
    }
    ctx.callbacks.remove_listener(ctx, time);
}

void drop_all(Context& ctx, TimePoint time)
{
    ctx.last_action = "drop_all";
    ctx.listener_count = 0;
    ctx.callbacks.drop_all(ctx, time);
}

}  // namespace statusbar::nanoavb::acmp_talker_sm
