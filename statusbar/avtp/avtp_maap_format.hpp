#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for AVTP MaapDu. Split from avtp_maap.hpp
/// so consumers that only need the data structures do not pay the
/// compile-time cost of <format>.

#include "statusbar/avtp/avtp_maap.hpp"
#include "statusbar/ieee/ieee_ethernet_format.hpp"
#include "statusbar/tsn/tsn_stream_id_format.hpp"

#include <format>

namespace statusbar::avtp {

/// Format a MaapDu to an output iterator
/// @param out The output iterator to write formatted text to
/// @param maap The MAAP PDU to format
template <typename OutputIt>
auto format_to(OutputIt out, MaapDu const& maap) -> OutputIt
{
    out = std::format_to(out, "MAAP: {} stream_id=", maap_message_type_name(maap.message_type()));
    out = tsn::format_to(out, maap.stream_id());

    out = std::format_to(out, "\n        requested: start=");
    out = ieee::format_to(out, maap.requested_start_address);
    out = std::format_to(out, " count={}", maap.requested_count.get());

    // Only show conflict fields for Defend messages or if they are non-zero
    uint16_t const conf_count = maap.conflict_count.get();
    if (maap.is_defend() || conf_count != 0) {
        out = std::format_to(out, "\n        conflict: start=");
        out = ieee::format_to(out, maap.conflict_start_address);
        out = std::format_to(out, " count={}", conf_count);
    }

    return out;
}

}  // namespace statusbar::avtp
