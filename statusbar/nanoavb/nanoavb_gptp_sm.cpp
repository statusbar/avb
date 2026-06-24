// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/nanoavb/nanoavb_gptp_sm.hpp"

#include "statusbar/sm/sm_core.hpp"

#include <functional>
#include <string>

namespace statusbar::nanoavb::gptp_sm {

void start_servo(Context& ctx, TimePoint time)
{
    ctx.last_action = function_name<&start_servo>;
    ctx.time_locked = false;
    ctx.callbacks.start_servo(ctx, time);
}

void report_locked(Context& ctx, TimePoint time)
{
    ctx.last_action = function_name<&report_locked>;
    ctx.time_locked = true;
    ctx.callbacks.report_locked(ctx, time);
}

void report_unlocked(Context& ctx, TimePoint time)
{
    ctx.last_action = function_name<&report_unlocked>;
    ctx.time_locked = false;
    ctx.callbacks.report_unlocked(ctx, time);
}

}  // namespace statusbar::nanoavb::gptp_sm
