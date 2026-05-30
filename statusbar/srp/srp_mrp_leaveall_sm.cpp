// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/srp/srp_mrp_leaveall_sm.hpp"

namespace statusbar::srp::mrp::leaveall_sm {

void a_init(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.clear_outputs();
    ctx.timer_restart = true;  // mrp.c:460 mrp_lvatimer_start
}

void a_tx_leaveall(Context& ctx, TimePoint /*time*/) noexcept
{
    // mrp.c:463-466: tx=1, sndmsg=LVA, state->Passive
    ctx.tx_leaveall_pending = true;
}

void a_restart_timer(Context& ctx, TimePoint /*time*/) noexcept
{
    // mrp.c:471-472 and mrp.c:476-477: stop + start = restart
    ctx.timer_restart = true;
}

}  // namespace statusbar::srp::mrp::leaveall_sm
