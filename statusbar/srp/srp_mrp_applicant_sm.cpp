// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/srp/srp_mrp_applicant_sm.hpp"

namespace statusbar::srp::mrp::applicant_sm {

void a_init(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.clear_outputs();
}

void a_tx_new(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.tx_pending = true;
    ctx.send_msg = SendMessage::New;
    ctx.encode = Encoding::Required;
}

void a_tx_join_required(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.tx_pending = true;
    ctx.send_msg = SendMessage::Join;
    ctx.encode = Encoding::Required;
}

void a_tx_join_optional(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.tx_pending = true;
    ctx.send_msg = SendMessage::Join;
    ctx.encode = Encoding::Optional;
}

void a_tx_in_required(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.tx_pending = true;
    ctx.send_msg = SendMessage::In;
    ctx.encode = Encoding::Required;
}

void a_tx_in_optional(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.tx_pending = true;
    ctx.send_msg = SendMessage::In;
    ctx.encode = Encoding::Optional;
}

void a_tx_leave(Context& ctx, TimePoint /*time*/) noexcept
{
    ctx.tx_pending = true;
    ctx.send_msg = SendMessage::Leave;
    ctx.encode = Encoding::Required;
}

}  // namespace statusbar::srp::mrp::applicant_sm
