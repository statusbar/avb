#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for ATDECC JDKS vendor-specific types. Split from
/// atdecc_jdks.hpp so consumers that only need the data structures do not pay
/// the compile-time cost of <format>.

#include "statusbar/atdecc/atdecc_jdks.hpp"
#include "statusbar/ieee/ieee_ethernet_format.hpp"

#include <format>

namespace statusbar::atdecc::jdks {

/// Format a LogBlobHeader to an output iterator
/// @param out Output iterator to write formatted text to
/// @param blob Log blob header to format
template <typename OutputIt>
auto format_to(OutputIt out, LogBlobHeader const& blob) -> OutputIt
{
    return std::format_to(
        out,
        "JDKS Log: priority={} ({}) blob_size={}",
        static_cast<uint8_t>(blob.log_detail),
        log_priority_name(static_cast<uint8_t>(blob.log_detail)),
        static_cast<uint32_t>(blob.blob_size));
}

/// Format a LogMessage to an output iterator
/// @param out Output iterator to write formatted text to
/// @param msg Log message to format
template <typename OutputIt>
auto format_to(OutputIt out, LogMessage const& msg) -> OutputIt
{
    out = std::format_to(out, "JDKS Log: {} ({}): \"{}\"", log_priority_name(msg.log_detail), msg.log_detail, msg.text);
    out = std::format_to(out, "\n        entity=");
    out = ieee::format_to(out, msg.source_entity_id);
    out = std::format_to(out, " desc_idx={} seq={}", msg.descriptor_index, msg.sequence_id);
    return out;
}

/// Format an Ipv4ParamsBlob to an output iterator
/// @param out Output iterator to write formatted text to
/// @param params IPv4 parameters blob to format
template <typename OutputIt>
auto format_to(OutputIt out, Ipv4ParamsBlob const& params) -> OutputIt
{
    out = std::format_to(
        out,
        "IPv4 Params: if={:#06x}:{} flags={:#010x}",
        static_cast<uint16_t>(params.interface_descriptor_type),
        static_cast<uint16_t>(params.interface_descriptor_index),
        static_cast<uint32_t>(params.flags));

    if (params.is_address_valid()) {
        uint32_t const addr = static_cast<uint32_t>(params.ipv4_address);
        out = std::format_to(
            out, "\n        address={}.{}.{}.{}", (addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF);
    }
    if (params.is_netmask_valid()) {
        uint32_t const mask = static_cast<uint32_t>(params.ipv4_netmask);
        out = std::format_to(
            out, "\n        netmask={}.{}.{}.{}", (mask >> 24) & 0xFF, (mask >> 16) & 0xFF, (mask >> 8) & 0xFF, mask & 0xFF);
    }
    if (params.is_gateway_valid()) {
        uint32_t const gw = static_cast<uint32_t>(params.ipv4_gateway);
        out =
            std::format_to(out, "\n        gateway={}.{}.{}.{}", (gw >> 24) & 0xFF, (gw >> 16) & 0xFF, (gw >> 8) & 0xFF, gw & 0xFF);
    }

    return out;
}

}  // namespace statusbar::atdecc::jdks
