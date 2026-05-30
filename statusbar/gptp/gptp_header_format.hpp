#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overload for gPTP MessageHeader. Split from gptp_header.hpp
/// so consumers that only need the header struct do not pay the
/// compile-time cost of <format>.

#include "statusbar/gptp/gptp_base_format.hpp"
#include "statusbar/gptp/gptp_header.hpp"

#include <format>

namespace statusbar::gptp {

/// Format a MessageHeader to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, MessageHeader const& hdr) -> OutputIt
{
    out = std::format_to(
        out,
        "gPTP: {} seq={} domain={} ver={}.{}\n        source=",
        message_type_name(hdr.message_type()),
        hdr.sequence_id.get(),
        hdr.domain_number.get(),
        hdr.version_ptp(),
        hdr.minor_version_ptp());
    return format_to(out, hdr.source_port_identity);
}

}  // namespace statusbar::gptp
