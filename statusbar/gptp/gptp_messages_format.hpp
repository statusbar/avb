#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for gPTP message types. Split from gptp_messages.hpp
/// so consumers that only need the message structs do not pay the
/// compile-time cost of <format>.

#include "statusbar/gptp/gptp_header_format.hpp"
#include "statusbar/gptp/gptp_messages.hpp"

#include <format>
#include <variant>

namespace statusbar::gptp {

/// Format a SyncMessage to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, SyncMessage const& msg) -> OutputIt
{
    out = format_to(out, msg.header);
    out = std::format_to(out, "\n        origin_ts=");
    return format_to(out, msg.origin_timestamp);
}

/// Format a FollowUpMessage to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, FollowUpMessage const& msg) -> OutputIt
{
    out = format_to(out, msg.header);
    out = std::format_to(out, "\n        precise_origin_ts=");
    return format_to(out, msg.precise_origin_timestamp);
}

/// Format a PdelayReqMessage to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, PdelayReqMessage const& msg) -> OutputIt
{
    out = format_to(out, msg.header);
    out = std::format_to(out, "\n        origin_ts=");
    return format_to(out, msg.origin_timestamp);
}

/// Format a PdelayRespMessage to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, PdelayRespMessage const& msg) -> OutputIt
{
    out = format_to(out, msg.header);
    out = std::format_to(out, "\n        request_receipt_ts=");
    out = format_to(out, msg.request_receipt_timestamp);
    out = std::format_to(out, "\n        requesting_port=");
    return format_to(out, msg.requesting_port_identity);
}

/// Format a PdelayRespFollowUpMessage to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, PdelayRespFollowUpMessage const& msg) -> OutputIt
{
    out = format_to(out, msg.header);
    out = std::format_to(out, "\n        response_origin_ts=");
    out = format_to(out, msg.response_origin_timestamp);
    out = std::format_to(out, "\n        requesting_port=");
    return format_to(out, msg.requesting_port_identity);
}

/// Format an AnnounceMessage to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, AnnounceMessage const& msg) -> OutputIt
{
    out = format_to(out, msg.header);
    out = std::format_to(out, "\n        grandmaster=");
    out = format_to(out, msg.grandmaster_identity);
    out = std::format_to(
        out, "\n        priority1={} priority2={}", msg.grandmaster_priority1.get(), msg.grandmaster_priority2.get());
    out = std::format_to(out, "\n        clock_quality=");
    out = format_to(out, msg.grandmaster_clock_quality);
    out = std::format_to(out, "\n        steps_removed={}", msg.steps_removed.get());
    return std::format_to(out, "\n        utc_offset={} time_source={}", msg.current_utc_offset.get(), msg.time_source.get());
}

/// Format a SignalingMessage to an output iterator (fixed portion only).
template <typename OutputIt>
auto format_to(OutputIt out, SignalingMessage const& msg) -> OutputIt
{
    out = format_to(out, msg.header);
    out = std::format_to(out, "\n        target_port=");
    return format_to(out, msg.target_port_identity);
}

/// Format a GptpTruncated to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, GptpTruncated const& msg) -> OutputIt
{
    out = format_to(out, msg.header);
    return std::format_to(out, "\n        (truncated)");
}

/// Format a GptpMessage variant to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, GptpMessage const& msg) -> OutputIt
{
    return std::visit([&out](auto const& m) -> OutputIt { return format_to(out, m); }, msg);
}

}  // namespace statusbar::gptp
