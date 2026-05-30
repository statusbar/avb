// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/nanoavb/nanoavb_entity.hpp"

namespace statusbar::nanoavb {

auto AemCommandHandler::process_packet(Eui48 const& src_mac, std::span<uint8_t const> payload, Eui64 const& our_entity_id) -> bool
{
    // Validate minimum size
    if (payload.size() < AemDu::LENGTH) {
        return false;
    }

    // Parse header
    AemDu header{};
    span_load(header, payload);

    // Validate header
    if (!header.is_valid() || !header.is_command()) {
        return false;
    }

    // Check target_entity_id matches us
    if (header.target_entity_id != our_entity_id) {
        return false;  // Not for us
    }

    // Stash the source MAC so per-command handlers can retain it if they
    // need to emit an out-of-band late response (e.g. ACQUIRE_ENTITY's
    // CONTROLLER_AVAILABLE handshake). Cleared after dispatch.
    current_src_mac_ = src_mac;

    // Extract command data (after AemDu header)
    auto const command_data = payload.subspan(AemDu::LENGTH);

    // One output buffer per dispatch — handlers write into this and return
    // status + bytes-written, avoiding the per-return 1 KB struct copy that
    // the previous AemCommandResult shape incurred.
    std::array<uint8_t, MAX_AEM_RESPONSE_SIZE> out_buffer{};
    auto const response = handle_command(header, command_data, out_buffer);

    current_src_mac_ = {};

    // Build and send response
    return send_response(src_mac, header, response.status, std::span<uint8_t const>{out_buffer.data(), response.size});
}

auto AemCommandHandler::handle_command(AemDu const& header, std::span<uint8_t const> command_data, std::span<uint8_t> out_buffer)
    -> AemCommandResponse
{
    auto const cmd = header.command_code();

    switch (cmd) {
        case AEM_COMMAND_READ_DESCRIPTOR:
            return handle_read_descriptor(header, command_data, out_buffer);

        case AEM_COMMAND_ENTITY_AVAILABLE:
            return handle_entity_available(header);

        case AEM_COMMAND_ACQUIRE_ENTITY:
            return handle_acquire_entity(header, command_data, out_buffer);

        case AEM_COMMAND_LOCK_ENTITY:
            return handle_lock_entity(header, command_data, out_buffer);

        case AEM_COMMAND_GET_CONFIGURATION:
            return handle_get_configuration(header, out_buffer);

        case AEM_COMMAND_CONTROLLER_AVAILABLE:
            return handle_controller_available(header);

        case AEM_COMMAND_SET_CONTROL:
            return handle_set_control(header, command_data);

        case AEM_COMMAND_GET_CONTROL:
            return handle_get_control(header, command_data);

        case AEM_COMMAND_GET_COUNTERS:
            return handle_get_counters(header, command_data);

        case AEM_COMMAND_REGISTER_UNSOLICITED_NOTIFICATION:
            return handle_register_unsolicited(header);

        case AEM_COMMAND_DEREGISTER_UNSOLICITED_NOTIFICATION:
            return handle_deregister_unsolicited(header);

        default:
            return {.status = AEM_STATUS_NOT_IMPLEMENTED, .size = 0};
    }
}

auto AemCommandHandler::handle_read_descriptor(
    AemDu const& /*header*/, std::span<uint8_t const> command_data, std::span<uint8_t> out_buffer) -> AemCommandResponse
{
    // READ_DESCRIPTOR command format:
    // Bytes 0-1: Configuration index
    // Bytes 2-3: Reserved
    // Bytes 4-5: Descriptor type
    // Bytes 6-7: Descriptor index

    if (command_data.size() < 8) {
        return {.status = AEM_STATUS_BAD_ARGUMENTS, .size = 0};
    }

    // Parse command parameters (network byte order)
    doublet_t config_index_net;
    doublet_t descriptor_type_net;
    doublet_t descriptor_index_net;
    span_load(config_index_net, command_data.subspan(0));
    span_load(descriptor_type_net, command_data.subspan(4));
    span_load(descriptor_index_net, command_data.subspan(6));

    uint16_t const config_index = config_index_net;          // Implicit conversion to host order
    uint16_t const descriptor_type = descriptor_type_net;    // Implicit conversion to host order
    uint16_t const descriptor_index = descriptor_index_net;  // Implicit conversion to host order

    // Build response data:
    // Bytes 0-1: Configuration index
    // Bytes 2-3: Reserved
    // Bytes 4-5: Descriptor type
    // Bytes 6-7: Descriptor index
    // Bytes 8+: Descriptor data

    // Need room for the 8-byte READ_DESCRIPTOR header plus at least a
    // minimal descriptor payload.
    if (out_buffer.size() <= 8) {
        return {.status = AEM_STATUS_NOT_SUPPORTED, .size = 0};
    }

    // Echo back the command parameters using network byte order types.
    doublet_t const config_idx_net = config_index;
    doublet_t const reserved{0};
    doublet_t const desc_type_net = descriptor_type;
    doublet_t const desc_idx_net = descriptor_index;

    span_store(out_buffer.subspan(0), config_idx_net);
    span_store(out_buffer.subspan(2), reserved);
    span_store(out_buffer.subspan(4), desc_type_net);
    span_store(out_buffer.subspan(6), desc_idx_net);

    // Dispatch to a locally-constructed AemEntityModel, which routes by
    // descriptor_type through a constexpr table to the appropriate
    // AemEntityHandler method. The handler_ pointer resolves to either
    // an internally-owned EntityModelAdapter (when this object was built
    // from a legacy EntityModel) or a caller-supplied AemEntityHandler.
    // Constructing the model per-call is free — it is two pointers plus
    // an optional — and avoids a dangling self-reference across moves.
    AemEntityModel const aem_model{*handler_};
    DescriptorRef const ref{
        .configuration_index = config_index, .descriptor_type = descriptor_type, .descriptor_index = descriptor_index};
    auto const bytes_written = aem_model.get_descriptor_for_wire(ref, out_buffer.subspan(8));
    if (bytes_written == 0) {
        return {.status = AEM_STATUS_NO_SUCH_DESCRIPTOR, .size = 0};
    }

    return {.status = AEM_STATUS_SUCCESS, .size = 8 + bytes_written};
}

auto AemCommandHandler::handle_acquire_entity(
    AemDu const& header, std::span<uint8_t const> command_data, std::span<uint8_t> out_buffer) -> AemCommandResponse
{
    // ACQUIRE_ENTITY command format:
    // Bytes 0-3: Flags (PERSISTENT=0x01, RELEASE=0x80000000)
    // Bytes 4-11: Owner ID (for query or release)
    // Bytes 12-13: Descriptor type
    // Bytes 14-15: Descriptor index

    if (command_data.size() < 16) {
        return {.status = AEM_STATUS_BAD_ARGUMENTS, .size = 0};
    }

    // Parse flags using network byte order type
    quadlet_t flags_net;
    span_load(flags_net, command_data);
    uint32_t const flags = flags_net;  // Implicit conversion to host order

    bool const release = (flags & 0x80000000) != 0;

    if (release) {
        // Release acquisition
        if (acquired_ && acquiring_controller_ == header.controller_entity_id) {
            acquired_ = false;
            acquired_persistent_ = false;
            acquiring_controller_ = {};
            return build_acquire_response(
                {.status = AEM_STATUS_SUCCESS, .flags = 0, .entity_id = {}, .command_data = command_data}, out_buffer);
        }
        return build_acquire_response(
            {.status = AEM_STATUS_ENTITY_ACQUIRED,
             .flags = flags,
             .entity_id = acquiring_controller_,
             .command_data = command_data},
            out_buffer);
    }

    // Acquire
    bool const persistent = (flags & 0x01) != 0;
    if (!acquired_) {
        acquired_ = true;
        acquired_persistent_ = persistent;
        acquiring_controller_ = header.controller_entity_id;
        return build_acquire_response(
            {.status = AEM_STATUS_SUCCESS, .flags = flags, .entity_id = acquiring_controller_, .command_data = command_data},
            out_buffer);
    }
    if (acquiring_controller_ == header.controller_entity_id) {
        // Already acquired by this controller
        return build_acquire_response(
            {.status = AEM_STATUS_SUCCESS, .flags = flags, .entity_id = acquiring_controller_, .command_data = command_data},
            out_buffer);
    }
    // Acquired by another controller - initiate CONTROLLER_AVAILABLE handshake
    // Skip if existing acquisition is persistent or no callback available
    if (!acquired_persistent_ && send_controller_available) {
        // Store pending acquire and send CONTROLLER_AVAILABLE to current owner
        pending_acquire_.requester_header = header;
        pending_acquire_.requester_mac = current_src_mac_;
        pending_acquire_.requester_flags = flags;
        std::copy_n(
            command_data.begin(),
            std::min(command_data.size(), pending_acquire_.command_data_copy.size()),
            pending_acquire_.command_data_copy.begin());
        pending_acquire_.timeout = event_time_ + std::chrono::milliseconds(AEM_TIMEOUT_MS);
        pending_acquire_.active = true;

        send_controller_available(acquiring_controller_);

        // Return IN_PROGRESS - the final response will be sent by
        // controller_available_response_received() or controller_available_timed_out()
        return {.status = AEM_STATUS_IN_PROGRESS, .size = 0};
    }

    // No CONTROLLER_AVAILABLE callback or PERSISTENT - deny immediately
    return build_acquire_response(
        {.status = AEM_STATUS_ENTITY_ACQUIRED, .flags = flags, .entity_id = acquiring_controller_, .command_data = command_data},
        out_buffer);
}

auto AemCommandHandler::build_acquire_response(AcquireLockResponseParams const& params, std::span<uint8_t> out_buffer)
    -> AemCommandResponse
{
    // Flags (network byte order)
    quadlet_t const flags_net = params.flags;
    span_store(out_buffer.subspan(0), flags_net);

    // Owner ID (8 bytes)
    std::copy(params.entity_id.span().begin(), params.entity_id.span().end(), out_buffer.data() + 4);

    // Echo descriptor type and index from command
    if (params.command_data.size() >= 16) {
        out_buffer[12] = params.command_data[12];
        out_buffer[13] = params.command_data[13];
        out_buffer[14] = params.command_data[14];
        out_buffer[15] = params.command_data[15];
    }

    return {.status = params.status, .size = 16};
}

auto AemCommandHandler::handle_lock_entity(
    AemDu const& header, std::span<uint8_t const> command_data, std::span<uint8_t> out_buffer) -> AemCommandResponse
{
    // LOCK_ENTITY command format:
    // Bytes 0-3: Flags (UNLOCK=0x01)
    // Bytes 4-11: Locked ID
    // Bytes 12-13: Descriptor type
    // Bytes 14-15: Descriptor index

    if (command_data.size() < 16) {
        return {.status = AEM_STATUS_BAD_ARGUMENTS, .size = 0};
    }

    // Parse flags using network byte order type
    quadlet_t flags_net;
    span_load(flags_net, command_data);
    uint32_t const flags = flags_net;  // Implicit conversion to host order

    bool const unlock = (flags & 0x01) != 0;

    if (unlock) {
        // Unlock
        if (locked_ && locking_controller_ == header.controller_entity_id) {
            locked_ = false;
            locking_controller_ = {};
            return build_lock_response(
                {.status = AEM_STATUS_SUCCESS, .flags = flags, .entity_id = {}, .command_data = command_data}, out_buffer);
        }
        if (!locked_) {
            return build_lock_response(
                {.status = AEM_STATUS_SUCCESS, .flags = flags, .entity_id = {}, .command_data = command_data}, out_buffer);
        }
        return build_lock_response(
            {.status = AEM_STATUS_ENTITY_LOCKED, .flags = flags, .entity_id = locking_controller_, .command_data = command_data},
            out_buffer);
    }

    // Lock
    if (!locked_) {
        locked_ = true;
        locking_controller_ = header.controller_entity_id;
        lock_expiry_time_ = event_time_ + std::chrono::milliseconds(AEM_LOCK_TIMEOUT_MS);
        return build_lock_response(
            {.status = AEM_STATUS_SUCCESS, .flags = flags, .entity_id = locking_controller_, .command_data = command_data},
            out_buffer);
    }
    if (locking_controller_ == header.controller_entity_id) {
        // Already locked by this controller - refresh timeout
        lock_expiry_time_ = event_time_ + std::chrono::milliseconds(AEM_LOCK_TIMEOUT_MS);
        return build_lock_response(
            {.status = AEM_STATUS_SUCCESS, .flags = flags, .entity_id = locking_controller_, .command_data = command_data},
            out_buffer);
    }
    // Locked by another controller
    return build_lock_response(
        {.status = AEM_STATUS_ENTITY_LOCKED, .flags = flags, .entity_id = locking_controller_, .command_data = command_data},
        out_buffer);
}

auto AemCommandHandler::build_lock_response(AcquireLockResponseParams const& params, std::span<uint8_t> out_buffer)
    -> AemCommandResponse
{
    // Flags (network byte order)
    quadlet_t const flags_net = params.flags;
    span_store(out_buffer.subspan(0), flags_net);

    // Locker ID (8 bytes)
    std::copy(params.entity_id.span().begin(), params.entity_id.span().end(), out_buffer.data() + 4);

    // Echo descriptor type and index from command
    if (params.command_data.size() >= 16) {
        out_buffer[12] = params.command_data[12];
        out_buffer[13] = params.command_data[13];
        out_buffer[14] = params.command_data[14];
        out_buffer[15] = params.command_data[15];
    }

    return {.status = params.status, .size = 16};
}

auto AemCommandHandler::handle_get_configuration(AemDu const& /*header*/, std::span<uint8_t> out_buffer) const -> AemCommandResponse
{
    // GET_CONFIGURATION response:
    // Bytes 0-1: Reserved
    // Bytes 2-3: Configuration index

    doublet_t const reserved{0};
    doublet_t const config_idx_net = current_configuration_;

    span_store(out_buffer.subspan(0), reserved);
    span_store(out_buffer.subspan(2), config_idx_net);

    return {.status = AEM_STATUS_SUCCESS, .size = 4};
}

auto AemCommandHandler::send_response(
    Eui48 const& dest_mac, AemDu const& cmd_header, uint8_t status, std::span<uint8_t const> response_body) const -> bool
{
    if (!callbacks_.send_response) {
        return false;
    }

    // Build response packet: AemDu header + response data
    std::array<uint8_t, AemDu::LENGTH + MAX_AEM_RESPONSE_SIZE> response_buffer{};

    // Copy and modify header for response
    AemDu response_header = cmd_header;
    response_header.init_response(
        cmd_header.command_code(),
        status,
        static_cast<uint16_t>(AemDu::AEM_DATA_LENGTH + response_body.size()),
        cmd_header.is_unsolicited());

    // Copy header to buffer
    auto buffer_span = std::span{response_buffer};
    span_store(buffer_span, response_header);

    // Copy response body
    if (!response_body.empty()) {
        span_copy(buffer_span.subspan(AemDu::LENGTH), response_body);
    }

    size_t const total_size = AemDu::LENGTH + response_body.size();
    return callbacks_.send_response(dest_mac, {response_buffer.data(), total_size});
}

auto AemCommandHandler::handle_register_unsolicited(AemDu const& header) -> AemCommandResponse
{
    // Idempotent: if this controller is already registered, return success.
    auto const existing = unsolicited_registrations_.find_if(
        [&header](UnsolicitedRegistration const& r) { return r.controller_entity_id == header.controller_entity_id; });
    if (existing < unsolicited_registrations_.capacity()) {
        return {.status = AEM_STATUS_SUCCESS, .size = 0};
    }

    if (!unsolicited_registrations_.add(UnsolicitedRegistration{.controller_entity_id = header.controller_entity_id})) {
        return {.status = AEM_STATUS_NO_RESOURCES, .size = 0};
    }
    return {.status = AEM_STATUS_SUCCESS, .size = 0};
}

auto AemCommandHandler::handle_deregister_unsolicited(AemDu const& header) -> AemCommandResponse
{
    auto const idx = unsolicited_registrations_.find_if(
        [&header](UnsolicitedRegistration const& r) { return r.controller_entity_id == header.controller_entity_id; });
    if (idx < unsolicited_registrations_.capacity()) {
        unsolicited_registrations_.remove(idx);
    }
    // Deregister is idempotent — always return success, even for unknown controllers.
    return {.status = AEM_STATUS_SUCCESS, .size = 0};
}

auto AemCommandHandler::unsolicited_registration_count() const noexcept -> size_t
{
    return unsolicited_registrations_.size();
}

void AemCommandHandler::controller_available_response_received()
{
    if (!pending_acquire_.active) {
        return;
    }

    // Current owner is alive — deny the pending requester with ENTITY_ACQUIRED.
    // The final response is sent here via the send_response callback rather
    // than being returned to the caller, because the IN_PROGRESS path in
    // process_packet() has already returned by the time we get here.
    std::array<uint8_t, MAX_AEM_RESPONSE_SIZE> out_buffer{};
    auto const original_command_data = std::span<uint8_t const>{pending_acquire_.command_data_copy};
    auto const response = build_acquire_response(
        {.status = AEM_STATUS_ENTITY_ACQUIRED,
         .flags = pending_acquire_.requester_flags,
         .entity_id = acquiring_controller_,
         .command_data = original_command_data},
        out_buffer);
    (void)send_response(
        pending_acquire_.requester_mac,
        pending_acquire_.requester_header,
        response.status,
        std::span<uint8_t const>{out_buffer.data(), response.size});

    pending_acquire_ = {};
}

void AemCommandHandler::controller_available_timed_out()
{
    if (!pending_acquire_.active) {
        return;
    }

    // Current owner is gone — transfer acquisition to the requester and
    // send a SUCCESS response. See controller_available_response_received
    // above for why the response is emitted here rather than returned.
    acquired_ = true;
    acquiring_controller_ = pending_acquire_.requester_header.controller_entity_id;
    acquired_persistent_ = (pending_acquire_.requester_flags & 0x01u) != 0u;

    std::array<uint8_t, MAX_AEM_RESPONSE_SIZE> out_buffer{};
    auto const original_command_data = std::span<uint8_t const>{pending_acquire_.command_data_copy};
    auto const response = build_acquire_response(
        {.status = AEM_STATUS_SUCCESS,
         .flags = pending_acquire_.requester_flags,
         .entity_id = acquiring_controller_,
         .command_data = original_command_data},
        out_buffer);
    (void)send_response(
        pending_acquire_.requester_mac,
        pending_acquire_.requester_header,
        response.status,
        std::span<uint8_t const>{out_buffer.data(), response.size});

    pending_acquire_ = {};
}

}  // namespace statusbar::nanoavb
