#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for AVTP Am824Pdu. Split from avtp_am824.hpp
/// so consumers that only need the data structures do not pay the
/// compile-time cost of <format>.

#include "statusbar/avtp/avtp_am824.hpp"
#include "statusbar/tsn/tsn_stream_id_format.hpp"

#include <format>

namespace statusbar::avtp {

/// Format an Am824Pdu to an output iterator
/// @param out The output iterator to write formatted text to
/// @param pdu The AM824 PDU to format
template <typename OutputIt>
auto format_to(OutputIt out, Am824Pdu const& pdu) -> OutputIt
{
    out = std::format_to(out, "AM824: stream_id=");
    out = tsn::format_to(out, pdu.stream_id());
    out = std::format_to(out, " seq={}", pdu.sequence_num());
    out = std::format_to(
        out,
        "\n       channels={} samples={} rate={}",
        pdu.channel_count(),
        pdu.sample_count(),
        am824_sample_rate_name(pdu.sample_rate()));
    out = std::format_to(out, "\n       timestamp={} dbc={}", pdu.avtp_timestamp(), pdu.data_block_count());
    if (pdu.cip_header.syt_valid()) {
        out = std::format_to(out, " syt={:#06x}", pdu.syt_timestamp());
    }
    return out;
}

}  // namespace statusbar::avtp
