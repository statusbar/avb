#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// ACMP formatting functions for pretty-printing ACMPDU structures
/// IEEE 1722.1 Clause 8

#include "statusbar/atdecc/atdecc_acmp_pdu.hpp"
#include "statusbar/ieee/ieee_ethernet_format.hpp"
#include "statusbar/ip/ip_ipv6_format.hpp"

#include <format>

namespace statusbar::atdecc {

//
// ACMP Flags Formatting
//
/// Format ACMP flags to an output iterator as comma-separated list
/// @param out Output iterator to write formatted text to
/// @param f ACMP flags bitmask value
template <typename OutputIt>
auto format_acmp_flags_to(OutputIt out, uint16_t f) -> OutputIt
{
    bool first = true;
    auto add = [&](char const* name) -> void {
        if (!first) {
            out = std::format_to(out, ",");
        }
        out = std::format_to(out, "{}", name);
        first = false;
    };

    if (f & acmp_flags::CLASS_B) {
        add("CLASS_B");
    }
    if (f & acmp_flags::FAST_CONNECT) {
        add("FAST_CONNECT");
    }
    if (f & acmp_flags::SAVED_STATE) {
        add("SAVED_STATE");
    }
    if (f & acmp_flags::STREAMING_WAIT) {
        add("STREAMING_WAIT");
    }
    if (f & acmp_flags::SUPPORTS_ENCRYPTED) {
        add("SUPPORTS_ENCRYPTED");
    }
    if (f & acmp_flags::ENCRYPTED_PDU) {
        add("ENCRYPTED_PDU");
    }
    if (f & acmp_flags::SRP_REGISTRATION_FAILED) {
        add("SRP_REGISTRATION_FAILED");
    }
    if (f & acmp_flags::CL_ENTRIES_VALID) {
        add("CL_ENTRIES_VALID");
    }
    if (f & acmp_flags::NO_SRP) {
        add("NO_SRP");
    }
    if (f & acmp_flags::UDP) {
        add("UDP");
    }

    if (first) {
        out = std::format_to(out, "(none)");
    }
    return out;
}

/// Format an AcmpDu to an output iterator
/// @param out Output iterator to write formatted text to
/// @param acmp ACMPDU to format
template <typename OutputIt>
auto format_to(OutputIt out, AcmpDu const& acmp) -> OutputIt
{
    out = std::format_to(out, "ACMP: {} ", acmp_message_type_name(acmp.message_type()));

    if (acmp.is_response()) {
        out = std::format_to(out, "status={} ({}) ", acmp.status(), acmp_status_name(acmp.status()));
    }

    out = std::format_to(out, "seq={}", acmp.sequence_id.get());

    out = std::format_to(out, "\n        controller=");
    out = ieee::format_to(out, acmp.controller_entity_id);

    out = std::format_to(out, "\n        talker=");
    out = ieee::format_to(out, acmp.talker_entity_id);
    out = std::format_to(out, " unique_id={}", acmp.talker_unique_id.get());

    out = std::format_to(out, "\n        listener=");
    out = ieee::format_to(out, acmp.listener_entity_id);
    out = std::format_to(out, " unique_id={}", acmp.listener_unique_id.get());

    out = std::format_to(out, "\n        stream_id=");
    out = ieee::format_to(out, acmp.stream_id);
    out = std::format_to(out, " dest_mac=");
    out = ieee::format_to(out, acmp.stream_dest_mac);

    out = std::format_to(out, "\n        connection_count={}", acmp.connection_count.get());

    uint16_t const f = acmp.flags.get();
    out = std::format_to(out, " flags={:#06x} [", f);
    out = format_acmp_flags_to(out, f);
    out = std::format_to(out, "]");

    uint16_t const vlan = acmp.stream_vlan_id.get();
    if (vlan != 0) {
        out = std::format_to(out, " vlan_id={}", vlan);
    }

    return out;
}

/// Format an AcmpDu2021 to an output iterator
/// @param out Output iterator to write formatted text to
/// @param acmp Extended ACMPDU (IEEE 1722.1-2021) to format
template <typename OutputIt>
auto format_to(OutputIt out, AcmpDu2021 const& acmp) -> OutputIt
{
    out = std::format_to(out, "ACMP(ext): {} ", acmp_message_type_name(acmp.message_type()));

    if (acmp.is_response()) {
        out = std::format_to(out, "status={} ({}) ", acmp.status(), acmp_status_name(acmp.status()));
    }

    out = std::format_to(out, "seq={}", acmp.sequence_id.get());

    out = std::format_to(out, "\n        controller=");
    out = ieee::format_to(out, acmp.controller_entity_id);

    out = std::format_to(out, "\n        talker=");
    out = ieee::format_to(out, acmp.talker_entity_id);
    out = std::format_to(out, " unique_id={}", acmp.talker_unique_id.get());

    out = std::format_to(out, "\n        listener=");
    out = ieee::format_to(out, acmp.listener_entity_id);
    out = std::format_to(out, " unique_id={}", acmp.listener_unique_id.get());

    out = std::format_to(out, "\n        stream_id=");
    out = ieee::format_to(out, acmp.stream_id);
    out = std::format_to(out, " dest_mac=");
    out = ieee::format_to(out, acmp.stream_dest_mac);

    out = std::format_to(out, "\n        connection_count={}", acmp.connection_count.get());

    uint16_t const f = acmp.flags.get();
    out = std::format_to(out, " flags={:#06x} [", f);
    out = format_acmp_flags_to(out, f);
    out = std::format_to(out, "]");

    uint16_t const vlan = acmp.stream_vlan_id.get();
    if (vlan != 0) {
        out = std::format_to(out, " vlan_id={}", vlan);
    }

    // Extended fields
    if (acmp.is_udp()) {
        out = std::format_to(out, "\n        udp: src_port={} dst_port={}", acmp.source_port.get(), acmp.destination_port.get());

        out = std::format_to(out, "\n        src_ip=");
        out = ip::format_to(out, acmp.source_ip_address);

        out = std::format_to(out, "\n        dst_ip=");
        out = ip::format_to(out, acmp.destination_ip_address);
    }

    return out;
}

}  // namespace statusbar::atdecc
