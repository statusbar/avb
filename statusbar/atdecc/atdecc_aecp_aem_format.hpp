#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for ATDECC AEM (AECP) types. Split from
/// atdecc_aecp_aem.hpp so consumers that only need the data structures do not
/// pay the compile-time cost of <format>.

#include "statusbar/atdecc/atdecc_aecp_aem.hpp"
#include "statusbar/ieee/ieee_ethernet_format.hpp"

#include <format>

namespace statusbar::atdecc {

/// Format an AemDu to an output iterator
/// @param out Output iterator to write formatted text to
/// @param aem AEM PDU header to format
template <typename OutputIt>
auto format_to(OutputIt out, AemDu const& aem) -> OutputIt
{
    out = std::format_to(out, "AEM: ");

    if (aem.is_response()) {
        out = std::format_to(out, "Response ");
        if (aem.is_unsolicited()) {
            out = std::format_to(out, "(unsolicited) ");
        }
        out = std::format_to(out, "status={} ({}) ", aem.status(), aem_status_name(aem.status()));
    } else {
        out = std::format_to(out, "Command ");
    }

    out = std::format_to(out, "{} ({:#06x})", aem_command_name(aem.command_code()), aem.command_code());
    out = std::format_to(out, " seq={} cdl={}", aem.sequence_id.get(), aem.control_data_length());

    out = std::format_to(out, "\n        target=");
    out = ieee::format_to(out, aem.target_entity_id);

    out = std::format_to(out, "\n        controller=");
    out = ieee::format_to(out, aem.controller_entity_id);

    return out;
}

}  // namespace statusbar::atdecc
