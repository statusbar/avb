#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for ATDECC AECP types. Split from atdecc_aecp.hpp so
/// consumers that only need the data structures do not pay the compile-time
/// cost of <format>.

#include "statusbar/atdecc/atdecc_aecp.hpp"
#include "statusbar/ieee/ieee_ethernet_format.hpp"

#include <format>

namespace statusbar::atdecc {

/// Format an AecpDuCommon to an output iterator
/// @param out Output iterator to write formatted text to
/// @param aecp AECP common header to format
template <typename OutputIt>
auto format_to(OutputIt out, AecpDuCommon const& aecp) -> OutputIt
{
    out = std::format_to(out, "AECP: {} ", aecp_message_type_name(aecp.message_type()));

    if (aecp.is_response()) {
        out = std::format_to(out, "status={} ({}) ", aecp.status(), aecp_status_name(aecp.status()));
    }

    out = std::format_to(out, "seq={} cdl={}", aecp.sequence_id.get(), aecp.control_data_length());

    out = std::format_to(out, "\n        target=");
    out = ieee::format_to(out, aecp.target_entity_id);

    out = std::format_to(out, "\n        controller=");
    out = ieee::format_to(out, aecp.controller_entity_id);

    return out;
}

}  // namespace statusbar::atdecc
