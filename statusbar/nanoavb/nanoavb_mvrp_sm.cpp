// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/nanoavb/nanoavb_mvrp_sm.hpp"

#include "statusbar/sm/sm_core.hpp"

#include <functional>
#include <string>

namespace statusbar::nanoavb::mvrp_sm {

void init(Context& ctx, TimePoint time)
{
    ctx.joined = false;
    ctx.callbacks.init(ctx, time);
}

void mark_joined(Context& ctx, TimePoint time)
{
    ctx.joined = true;
    ctx.callbacks.mark_joined(ctx, time);
}

void mark_left(Context& ctx, TimePoint time)
{
    ctx.joined = false;
    ctx.callbacks.mark_left(ctx, time);
}

void mark_error(Context& ctx, TimePoint time)
{
    ctx.joined = false;
    if (ctx.callbacks.mark_error) {
        ctx.callbacks.mark_error(ctx, time);
    }
}

void reset(Context& ctx, TimePoint time)
{
    ctx.joined = false;
    ctx.refcount = 0;
    ctx.callbacks.reset(ctx, time);
}

}  // namespace statusbar::nanoavb::mvrp_sm
