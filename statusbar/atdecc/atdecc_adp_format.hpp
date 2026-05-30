#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for ATDECC ADP types. Split from atdecc_adp.hpp so
/// consumers that only need the data structures do not pay the compile-time
/// cost of <format>.

#include "statusbar/atdecc/atdecc_adp.hpp"
#include "statusbar/ieee/ieee_ethernet_format.hpp"
#include "statusbar/tsn/tsn_clock_identity_format.hpp"

#include <format>
#include <string>

namespace statusbar::atdecc {

/// Format entity capabilities flags to an output iterator as comma-separated list
/// @param out Output iterator to write formatted text to
/// @param caps Entity capabilities bitmask value
template <typename OutputIt>
auto format_entity_capabilities_to(OutputIt out, uint32_t const caps) -> OutputIt
{
    bool first = true;
    auto add = [&](char const* name) -> void {
        if (!first) {
            out = std::format_to(out, ",");
        }
        out = std::format_to(out, "{}", name);
        first = false;
    };

    if (caps & entity_capabilities::EFU_MODE) {
        add("EFU_MODE");
    }
    if (caps & entity_capabilities::ADDRESS_ACCESS_SUPPORTED) {
        add("ADDRESS_ACCESS");
    }
    if (caps & entity_capabilities::GATEWAY_ENTITY) {
        add("GATEWAY");
    }
    if (caps & entity_capabilities::AEM_SUPPORTED) {
        add("AEM");
    }
    if (caps & entity_capabilities::LEGACY_AVC) {
        add("LEGACY_AVC");
    }
    if (caps & entity_capabilities::ASSOCIATION_ID_SUPPORTED) {
        add("ASSOC_ID_SUPPORTED");
    }
    if (caps & entity_capabilities::ASSOCIATION_ID_VALID) {
        add("ASSOC_ID_VALID");
    }
    if (caps & entity_capabilities::VENDOR_UNIQUE_SUPPORTED) {
        add("VENDOR_UNIQUE");
    }
    if (caps & entity_capabilities::CLASS_A_SUPPORTED) {
        add("CLASS_A");
    }
    if (caps & entity_capabilities::CLASS_B_SUPPORTED) {
        add("CLASS_B");
    }
    if (caps & entity_capabilities::GPTP_SUPPORTED) {
        add("GPTP");
    }
    if (caps & entity_capabilities::AEM_AUTHENTICATION_SUPPORTED) {
        add("AEM_AUTH");
    }
    if (caps & entity_capabilities::AEM_AUTHENTICATION_REQUIRED) {
        add("AEM_AUTH_REQ");
    }
    if (caps & entity_capabilities::AEM_PERSISTENT_ACQUIRE_SUPPORTED) {
        add("PERSISTENT_ACQUIRE");
    }
    if (caps & entity_capabilities::AEM_IDENTIFY_CONTROL_INDEX_VALID) {
        add("IDENTIFY_CTRL_VALID");
    }
    if (caps & entity_capabilities::AEM_INTERFACE_INDEX_VALID) {
        add("IFACE_IDX_VALID");
    }
    if (caps & entity_capabilities::GENERAL_CONTROLLER_IGNORE) {
        add("CTRL_IGNORE");
    }
    if (caps & entity_capabilities::ENTITY_NOT_READY) {
        add("NOT_READY");
    }
    if (caps & entity_capabilities::ACMP_ACQUIRE_WITH_AEM) {
        add("ACMP_ACQUIRE");
    }
    if (caps & entity_capabilities::ACMP_AUTHENTICATE_WITH_AEM) {
        add("ACMP_AUTH");
    }
    if (caps & entity_capabilities::SUPPORTS_UDPV4_ATDECC) {
        add("UDPv4_ATDECC");
    }
    if (caps & entity_capabilities::SUPPORTS_UDPV4_STREAMING) {
        add("UDPv4_STREAM");
    }
    if (caps & entity_capabilities::SUPPORTS_UDPV6_ATDECC) {
        add("UDPv6_ATDECC");
    }
    if (caps & entity_capabilities::SUPPORTS_UDPV6_STREAMING) {
        add("UDPv6_STREAM");
    }
    if (caps & entity_capabilities::MULTIPLE_PTP_INSTANCES) {
        add("MULTI_PTP");
    }
    if (caps & entity_capabilities::AEM_CONFIGURATION_INDEX_VALID) {
        add("CONFIG_IDX_VALID");
    }

    if (first) {
        out = std::format_to(out, "(none)");
    }
    return out;
}

/// Convert entity capabilities flags to a comma-separated string
[[nodiscard]] inline auto entity_capabilities_to_string(uint32_t caps) -> std::string
{
    std::string result;
    format_entity_capabilities_to(std::back_inserter(result), caps);
    return result;
}

/// Format talker capabilities flags to an output iterator as comma-separated list
/// @param out Output iterator to write formatted text to
/// @param caps Talker capabilities bitmask value
template <typename OutputIt>
auto format_talker_capabilities_to(OutputIt out, uint16_t const caps) -> OutputIt
{
    bool first = true;
    auto add = [&](char const* name) -> void {
        if (!first) {
            out = std::format_to(out, ",");
        }
        out = std::format_to(out, "{}", name);
        first = false;
    };

    if (caps & talker_capabilities::IMPLEMENTED) {
        add("IMPLEMENTED");
    }
    if (caps & talker_capabilities::OTHER_SOURCE) {
        add("OTHER");
    }
    if (caps & talker_capabilities::CONTROL_SOURCE) {
        add("CONTROL");
    }
    if (caps & talker_capabilities::MEDIA_CLOCK_SOURCE) {
        add("MEDIA_CLK");
    }
    if (caps & talker_capabilities::SMPTE_SOURCE) {
        add("SMPTE");
    }
    if (caps & talker_capabilities::MIDI_SOURCE) {
        add("MIDI");
    }
    if (caps & talker_capabilities::AUDIO_SOURCE) {
        add("AUDIO");
    }
    if (caps & talker_capabilities::VIDEO_SOURCE) {
        add("VIDEO");
    }

    if (first) {
        out = std::format_to(out, "(none)");
    }
    return out;
}

/// Format listener capabilities flags to an output iterator as comma-separated list
/// @param out Output iterator to write formatted text to
/// @param caps Listener capabilities bitmask value
template <typename OutputIt>
auto format_listener_capabilities_to(OutputIt out, uint16_t const caps) -> OutputIt
{
    bool first = true;
    auto add = [&](char const* name) -> void {
        if (!first) {
            out = std::format_to(out, ",");
        }
        out = std::format_to(out, "{}", name);
        first = false;
    };

    if (caps & listener_capabilities::IMPLEMENTED) {
        add("IMPLEMENTED");
    }
    if (caps & listener_capabilities::OTHER_SINK) {
        add("OTHER");
    }
    if (caps & listener_capabilities::CONTROL_SINK) {
        add("CONTROL");
    }
    if (caps & listener_capabilities::MEDIA_CLOCK_SINK) {
        add("MEDIA_CLK");
    }
    if (caps & listener_capabilities::SMPTE_SINK) {
        add("SMPTE");
    }
    if (caps & listener_capabilities::MIDI_SINK) {
        add("MIDI");
    }
    if (caps & listener_capabilities::AUDIO_SINK) {
        add("AUDIO");
    }
    if (caps & listener_capabilities::VIDEO_SINK) {
        add("VIDEO");
    }

    if (first) {
        out = std::format_to(out, "(none)");
    }
    return out;
}

/// Format an AdpDu to an output iterator
/// @param out Output iterator to write formatted text to
/// @param adp ADPDU to format
template <typename OutputIt>
auto format_to(OutputIt out, AdpDu const& adp) -> OutputIt
{
    out = std::format_to(out, "ADP: {} entity_id=", adp_message_type_name(adp.message_type()));
    out = ieee::format_to(out, adp.entity_id);
    out = std::format_to(out, " valid_time={}s", adp.valid_time() * 2);

    if (adp.is_entity_available()) {
        out = std::format_to(out, "\n        model_id=");
        out = ieee::format_to(out, adp.entity_model_id);
        out = std::format_to(out, "\n        entity_caps={:#010x} [", adp.entity_capabilities.get());
        out = format_entity_capabilities_to(out, adp.entity_capabilities.get());
        out = std::format_to(out, "]\n        available_idx={}", adp.available_index.get());

        if (adp.has_talker_capability(talker_capabilities::IMPLEMENTED)) {
            out = std::format_to(
                out, "\n        talker: sources={} caps={:#06x} [", adp.talker_stream_sources.get(), adp.talker_capabilities.get());
            out = format_talker_capabilities_to(out, adp.talker_capabilities.get());
            out = std::format_to(out, "]");
        }

        if (adp.has_listener_capability(listener_capabilities::IMPLEMENTED)) {
            out = std::format_to(
                out,
                "\n        listener: sinks={} caps={:#06x} [",
                adp.listener_stream_sinks.get(),
                adp.listener_capabilities.get());
            out = format_listener_capabilities_to(out, adp.listener_capabilities.get());
            out = std::format_to(out, "]");
        }

        if (adp.has_controller_capability(controller_capabilities::IMPLEMENTED)) {
            out = std::format_to(out, "\n        controller: caps={:#010x}", adp.controller_capabilities.get());
        }

        if (adp.has_entity_capability(entity_capabilities::GPTP_SUPPORTED)) {
            out = std::format_to(out, "\n        gptp_gm=");
            out = tsn::format_to(out, adp.gptp_grandmaster_id);
            out = std::format_to(out, " domain={}", adp.gptp_domain_number.get());
        }

        if (adp.has_entity_capability(entity_capabilities::ASSOCIATION_ID_VALID)) {
            out = std::format_to(out, "\n        association_id=");
            out = ieee::format_to(out, adp.association_id);
        }

        if (adp.has_entity_capability(entity_capabilities::AEM_IDENTIFY_CONTROL_INDEX_VALID)) {
            out = std::format_to(out, "\n        identify_control_index={}", adp.identify_control_index.get());
        }

        if (adp.has_entity_capability(entity_capabilities::AEM_INTERFACE_INDEX_VALID)) {
            out = std::format_to(out, "\n        interface_index={}", adp.interface_index.get());
        }

        if (adp.has_entity_capability(entity_capabilities::AEM_CONFIGURATION_INDEX_VALID)) {
            out = std::format_to(out, "\n        current_configuration_index={}", adp.current_configuration_index.get());
        }
    }

    return out;
}

}  // namespace statusbar::atdecc
