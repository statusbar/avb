#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Shared model types for the ATDECC controller — consumed by both the
/// network-side facade (ControllerSimple) and any presentation layer
/// (ControllerTuiApp, Lua scripts, GUIs, autonomous services).
///
/// These types carry no UI state. They are the command/event/data protocol
/// between the business logic and whatever drives it.

#include "statusbar/ieee/ieee.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace statusbar::atdecc_tools {

//
// Command types (sent from a driver to the controller facade)
//

/// Describes a stream connection request (talker + listener) or AEM command target.
struct StreamRequest
{
    ieee::Eui64 talker_entity_id{};
    uint16_t talker_unique_id{0};
    ieee::Eui64 listener_entity_id{};
    uint16_t listener_unique_id{0};
    uint16_t desc_type{0};      ///< Descriptor type for AEM commands
    uint16_t desc_index{0};     ///< Descriptor index for AEM commands
    uint64_t stream_format{0};  ///< Stream format for SET_STREAM_FORMAT
};

/// Kind of controller action — every command a driver can issue.
enum class ControllerActionKind : uint8_t
{
    DiscoverAll,            ///< Send broadcast ENTITY_DISCOVER + refresh RX state queries
    ConnectStream,          ///< ACMP CONNECT_TX_COMMAND
    DisconnectStream,       ///< ACMP DISCONNECT_TX_COMMAND
    ReadEntityDescriptors,  ///< Read all descriptors for entity in request.talker_entity_id
    IdentifyEntity,         ///< Send IDENTIFY_NOTIFICATION for entity in request.talker_entity_id
    StartStreaming,         ///< Start streaming on stream (desc_type + desc_index)
    StopStreaming,          ///< Stop streaming on stream (desc_type + desc_index)
    SetStreamFormat,        ///< Set stream format (desc_type + desc_index + stream_format)
};

/// An action the driver wants the controller facade to perform.
struct ControllerAction
{
    ControllerActionKind kind{ControllerActionKind::DiscoverAll};
    StreamRequest request{};
};

//
// Data snapshot types (returned from controller facade queries)
//

/// Display information for a single discovered entity.
struct EntityDisplayInfo
{
    ieee::Eui64 entity_id{};
    std::string name;
    uint16_t talker_stream_sources{0};
    uint16_t listener_stream_sinks{0};
    bool has_talker{false};
    bool has_listener{false};
    std::vector<std::string> talker_stream_formats;
    std::vector<std::string> listener_stream_formats;
    std::vector<std::string> talker_stream_names;
    std::vector<std::string> listener_stream_names;
};

/// An active stream connection observed on the network.
struct ActiveConnection
{
    ieee::Eui64 talker_entity_id{};
    uint16_t talker_unique_id{0};
    ieee::Eui64 listener_entity_id{};
    uint16_t listener_unique_id{0};
    std::string talker_name;
    std::string listener_name;
    std::string talker_format;

    auto operator<=>(ActiveConnection const& o) const noexcept
    {
        if (auto c = talker_entity_id <=> o.talker_entity_id; c != 0) {
            return c;
        }
        if (auto c = talker_unique_id <=> o.talker_unique_id; c != 0) {
            return c;
        }
        if (auto c = listener_entity_id <=> o.listener_entity_id; c != 0) {
            return c;
        }
        return listener_unique_id <=> o.listener_unique_id;
    }
    auto operator==(ActiveConnection const& o) const noexcept -> bool
    {
        return talker_entity_id == o.talker_entity_id && talker_unique_id == o.talker_unique_id &&
            listener_entity_id == o.listener_entity_id && listener_unique_id == o.listener_unique_id;
    }
};

/// A single line in the entity detail view.
struct DetailLine
{
    std::string text;
    bool bold{false};  ///< Section header hint — consumer decides how to render
};

/// Accumulated descriptor information for a single entity.
struct EntityDetail
{
    ieee::Eui64 entity_id{};
    std::string name;
    std::vector<DetailLine> lines;
};

//
// Event types (asynchronous notifications from the controller facade)
//

struct ConnectionAddedEvent
{
    ActiveConnection connection;
};

struct ConnectionRemovedEvent
{
    ieee::Eui64 listener_entity_id{};
    uint16_t listener_unique_id{0};
};

struct EntityDetailReadyEvent
{
    EntityDetail detail;
};

struct StatusChangedEvent
{
    std::string status;
};

/// Variant covering every event the controller facade can emit.
using ControllerEvent = std::variant<ConnectionAddedEvent, ConnectionRemovedEvent, EntityDetailReadyEvent, StatusChangedEvent>;

}  // namespace statusbar::atdecc_tools
