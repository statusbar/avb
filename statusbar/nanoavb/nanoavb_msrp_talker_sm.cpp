// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/nanoavb/nanoavb_msrp_talker_sm.hpp"

#include "statusbar/sm/sm_core.hpp"

#include <functional>
#include <string>

namespace statusbar::nanoavb::msrp_talker_sm {

void init(Context& ctx, TimePoint time)
{
    ctx.last_action = "init";
    ctx.reserved = false;
    ctx.failed = false;
    ctx.callbacks.init(ctx, time);
}

void mark_ready(Context& ctx, TimePoint time)
{
    ctx.last_action = "mark_ready";
    ctx.reserved = true;
    ctx.failed = false;
    ctx.callbacks.mark_ready(ctx, time);
}

void mark_failed(Context& ctx, TimePoint time)
{
    ctx.last_action = "mark_failed";
    ctx.reserved = false;
    ctx.failed = true;
    ctx.callbacks.mark_failed(ctx, time);
}

void msrp_talker_withdraw(Context& ctx, TimePoint time)
{
    ctx.last_action = "msrp_talker_withdraw"; /* withdraw talker attr */
    ctx.reserved = false;
    ctx.callbacks.msrp_talker_withdraw(ctx, time);
}

void mark_idle(Context& ctx, TimePoint time)
{
    ctx.last_action = "mark_idle";
    ctx.reserved = false;
    ctx.failed = false;
    ctx.callbacks.mark_idle(ctx, time);
}

}  // namespace statusbar::nanoavb::msrp_talker_sm
