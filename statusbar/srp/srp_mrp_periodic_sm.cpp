// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/srp/srp_mrp_periodic_sm.hpp"

namespace statusbar::srp::mrp::periodic_sm {

void a_init(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.clear_outputs();
    ctx.timer_restart = true;  // mrp.c:521 mrp_periodictimer_start
}

void a_restart_timer(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.timer_restart = true;
}

}  // namespace statusbar::srp::mrp::periodic_sm
