// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/srp/srp_mrp_registrar_sm.hpp"

namespace statusbar::srp::mrp::registrar_sm {

void a_init(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.clear_outputs();
}

void a_notify_new(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.notify = Notify::New;
}

void a_notify_join(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.notify = Notify::Join;
}

void a_notify_lv(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.notify = Notify::Leave;
}

void a_start_lvtimer(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.lvtimer_request = true;
}

void a_notify_lv_and_start_lvtimer(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.notify = Notify::Leave;
    ctx.lvtimer_request = true;
}

}  // namespace statusbar::srp::mrp::registrar_sm
