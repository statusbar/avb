// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// ACMP Controller State Machine action implementations

#include "statusbar/atdecc/atdecc_acmp_controller_sm.hpp"

#include "statusbar/atdecc/atdecc_acmp.hpp"
#include "statusbar/ieee/ieee_ethernet.hpp"
#include "statusbar/sm/sm_core.hpp"

#include <chrono>
#include <cstddef>
#include <functional>

namespace statusbar::atdecc::controller_actions {

void send_command(ControllerContext& ctx, sm::TimePoint const event_time)
{
    // Refuse to send if there's no free inflight slot. Otherwise the
    // packet would go on the wire untracked and its response would be
    // silently ignored when `find_inflight` returns the not-found sentinel.
    if (ctx.inflight_count() >= ctx.max_inflight()) {
        return;
    }

    // Build command from params
    AcmpCommandResponse cmd{};
    cmd.init_command(ctx.command_params.message_type);
    cmd.controller_entity_id = ctx.my_id;
    cmd.talker_entity_id = ctx.command_params.talker_entity_id;
    cmd.listener_entity_id = ctx.command_params.listener_entity_id;
    cmd.talker_unique_id = ctx.command_params.talker_unique_id;
    cmd.listener_unique_id = ctx.command_params.listener_unique_id;
    cmd.connection_count = ctx.command_params.connection_count;
    cmd.sequence_id = ctx.next_sequence_id++;
    cmd.flags = ctx.command_params.flags;
    cmd.stream_vlan_id = ctx.command_params.stream_vlan_id;

    // Try to send
    if (ctx.tx_command && ctx.tx_command(cmd)) {
        // Add to inflight. We checked capacity above; in a single-threaded
        // context the table cannot have filled between then and now.
        auto timeout = acmp_timeout_for_message_type(cmd.message_type());
        auto timeout_time = event_time + timeout;
        [[maybe_unused]] bool const added = ctx.add_inflight(cmd, timeout_time);
    }
}

void handle_timeout(ControllerContext& ctx, sm::TimePoint const event_time)
{
    size_t const idx = ctx.current_inflight_index;
    auto* entry = ctx.get_inflight(idx);
    if (entry == nullptr) {
        return;
    }

    if (entry->retried) {
        // Already retried once, give up
        ctx.remove_inflight(idx);
    } else {
        // Retry the command
        entry->retried = true;
        auto timeout = acmp_timeout_for_message_type(entry->command.message_type());
        entry->timeout_time = event_time + timeout;

        if (ctx.tx_command) {
            ctx.tx_command(entry->command);
        }
    }
}

void handle_response(ControllerContext& ctx, sm::TimePoint /*event_time*/)
{
    size_t const idx = ctx.current_inflight_index;
    auto* entry = ctx.get_inflight(idx);
    if (entry == nullptr) {
        return;
    }

    // Call user's process_response callback
    if (ctx.process_response) {
        ctx.process_response(ctx.rcvd_cmd_resp);
    }

    // Remove from inflight
    ctx.remove_inflight(idx);
}

}  // namespace statusbar::atdecc::controller_actions

namespace statusbar::atdecc {

auto controller_should_handle_response(ControllerContext const& ctx, AcmpCommandResponse const& resp) noexcept -> bool
{
    // Must be a response (odd message type)
    if ((resp.message_type() & 0x01) == 0) {
        return false;
    }
    // Must be for this controller
    if (resp.controller_entity_id != ctx.my_id) {
        return false;
    }
    // Must match an inflight command
    return ctx.find_inflight(resp) < ctx.max_inflight();
}

}  // namespace statusbar::atdecc
