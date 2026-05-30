// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// ACMP Talker State Machine action implementations

#include "statusbar/atdecc/atdecc_acmp_talker_sm.hpp"

#include "statusbar/atdecc/atdecc_acmp.hpp"
#include "statusbar/ieee/ieee_ethernet.hpp"
#include "statusbar/sm/sm_core.hpp"

#include <cstdint>
#include <functional>

namespace statusbar::atdecc::talker_actions {

static void fill_stream_info_and_send(AcmpCommandResponse& resp, TalkerStreamInfoDynamic<> const& stream, TalkerContext<>& ctx)
{
    resp.stream_id = stream.stream_id;
    resp.stream_dest_mac = stream.stream_dest_mac;
    resp.stream_vlan_id = stream.stream_vlan_id;
    resp.connection_count = static_cast<uint16_t>(stream.connection_count());

    if (static_cast<bool>(ctx.tx_response)) {
        ctx.tx_response(resp);
    }
}

void handle_connect_tx(TalkerContext<>& ctx, sm::TimePoint /*event_time*/)
{
    auto& cmd = ctx.rcvd_cmd_resp;
    auto& resp = ctx.response;

    // Initialize response from command
    resp = cmd;
    resp.set_message_type(ACMP_MESSAGE_TYPE_CONNECT_TX_RESPONSE);

    // Validate stream exists
    auto* stream = ctx.get_stream(cmd.talker_unique_id.get());
    if (stream == nullptr) {
        resp.set_status(ACMP_STATUS_TALKER_UNKNOWN_ID);
        if (static_cast<bool>(ctx.tx_response)) {
            ctx.tx_response(resp);
        }
        return;
    }

    // Authorization check
    if (ctx.is_authorized && !ctx.is_authorized(cmd.controller_entity_id)) {
        resp.set_status(ACMP_STATUS_CONTROLLER_NOT_AUTHORIZED);
        if (static_cast<bool>(ctx.tx_response)) {
            ctx.tx_response(resp);
        }
        return;
    }

    // Try to add listener to stream's connected listeners
    if (!stream->add_listener({.listener_entity_id = cmd.listener_entity_id, .listener_unique_id = cmd.listener_unique_id.get()})) {
        // Already at max connections or already connected
        if (stream->contains_listener(
                {.listener_entity_id = cmd.listener_entity_id, .listener_unique_id = cmd.listener_unique_id.get()})) {
            // Already connected - success (idempotent)
            resp.set_status(ACMP_STATUS_SUCCESS);
        } else {
            resp.set_status(ACMP_STATUS_TALKER_NO_STREAM_INDEX);
        }
    } else {
        resp.set_status(ACMP_STATUS_SUCCESS);
    }

    fill_stream_info_and_send(resp, *stream, ctx);
}

void handle_disconnect_tx(TalkerContext<>& ctx, sm::TimePoint /*event_time*/)
{
    auto& cmd = ctx.rcvd_cmd_resp;
    auto& resp = ctx.response;

    // Initialize response from command
    resp = cmd;
    resp.set_message_type(ACMP_MESSAGE_TYPE_DISCONNECT_TX_RESPONSE);

    // Validate stream exists
    auto* stream = ctx.get_stream(cmd.talker_unique_id.get());
    if (stream == nullptr) {
        resp.set_status(ACMP_STATUS_TALKER_UNKNOWN_ID);
        if (static_cast<bool>(ctx.tx_response)) {
            ctx.tx_response(resp);
        }
        return;
    }

    // Authorization check
    if (ctx.is_authorized && !ctx.is_authorized(cmd.controller_entity_id)) {
        resp.set_status(ACMP_STATUS_CONTROLLER_NOT_AUTHORIZED);
        if (static_cast<bool>(ctx.tx_response)) {
            ctx.tx_response(resp);
        }
        return;
    }

    // Remove listener from stream's connected listeners
    stream->remove_listener({.listener_entity_id = cmd.listener_entity_id, .listener_unique_id = cmd.listener_unique_id.get()});
    resp.set_status(ACMP_STATUS_SUCCESS);

    fill_stream_info_and_send(resp, *stream, ctx);
}

void handle_get_tx_state(TalkerContext<>& ctx, sm::TimePoint /*event_time*/)
{
    auto& cmd = ctx.rcvd_cmd_resp;
    auto& resp = ctx.response;

    // Initialize response from command
    resp = cmd;
    resp.set_message_type(ACMP_MESSAGE_TYPE_GET_TX_STATE_RESPONSE);

    // Validate stream exists
    auto const* stream = ctx.get_stream(cmd.talker_unique_id.get());
    if (stream == nullptr) {
        resp.set_status(ACMP_STATUS_TALKER_UNKNOWN_ID);
        if (static_cast<bool>(ctx.tx_response)) {
            ctx.tx_response(resp);
        }
        return;
    }

    resp.set_status(ACMP_STATUS_SUCCESS);

    fill_stream_info_and_send(resp, *stream, ctx);
}

void handle_get_tx_connection(TalkerContext<>& ctx, sm::TimePoint /*event_time*/)
{
    auto& cmd = ctx.rcvd_cmd_resp;
    auto& resp = ctx.response;

    // Initialize response from command
    resp = cmd;
    resp.set_message_type(ACMP_MESSAGE_TYPE_GET_TX_CONNECTION_RESPONSE);

    // Validate stream exists
    auto const* stream = ctx.get_stream(cmd.talker_unique_id.get());
    if (stream == nullptr) {
        resp.set_status(ACMP_STATUS_TALKER_UNKNOWN_ID);
        if (static_cast<bool>(ctx.tx_response)) {
            ctx.tx_response(resp);
        }
        return;
    }

    // connection_count in the command is used as index
    uint16_t const index = cmd.connection_count.get();
    if (index >= stream->connection_count()) {
        resp.set_status(ACMP_STATUS_NO_SUCH_CONNECTION);
        resp.connection_count = static_cast<uint16_t>(stream->connection_count());
        if (static_cast<bool>(ctx.tx_response)) {
            ctx.tx_response(resp);
        }
        return;
    }

    resp.set_status(ACMP_STATUS_SUCCESS);

    // Return the listener at the given index
    auto const* listener = stream->get_listener(index);
    if (listener != nullptr) {
        resp.listener_entity_id = listener->listener_entity_id;
        resp.listener_unique_id = listener->listener_unique_id;
    }

    // Echo back the requested index per IEEE 1722.1 Clause 8.2.4.6 (instead of actual count)
    resp.stream_id = stream->stream_id;
    resp.stream_dest_mac = stream->stream_dest_mac;
    resp.stream_vlan_id = stream->stream_vlan_id;
    resp.connection_count = cmd.connection_count;

    if (static_cast<bool>(ctx.tx_response)) {
        ctx.tx_response(resp);
    }
}

}  // namespace statusbar::atdecc::talker_actions
