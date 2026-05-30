#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for AVTP Am824V1Pdu. Split from avtp_am824_v1.hpp
/// so consumers that only need the data structures do not pay the
/// compile-time cost of <format>.

#include "statusbar/avtp/avtp_am824.hpp"
#include "statusbar/avtp/avtp_am824_format.hpp"
#include "statusbar/avtp/avtp_am824_v1.hpp"
#include "statusbar/tsn/tsn_clock_identity_format.hpp"
#include "statusbar/tsn/tsn_stream_id_format.hpp"

#include <format>

namespace statusbar::avtp {

/// Format an Am824V1Pdu to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, Am824V1Pdu const& pdu) -> OutputIt
{
    out = std::format_to(out, "AM824(v1): stream_id=");
    out = tsn::format_to(out, pdu.stream_id());
    out = std::format_to(out, " seq={}", pdu.get_sequence_num());
    out = std::format_to(out, " gm=");
    out = tsn::format_to(out, pdu.get_ptp_grandmaster_identity());
    out = std::format_to(
        out,
        "\n         channels={} samples={} rate={}",
        pdu.channel_count(),
        pdu.sample_count(),
        am824_sample_rate_name(pdu.sample_rate()));
    out = std::format_to(out, "\n         timestamp={} dbc={}", pdu.avtp_timestamp(), pdu.data_block_count());
    if (pdu.cip_header.syt_valid()) {
        out = std::format_to(out, " syt={:#06x}", pdu.syt_timestamp());
    }
    return out;
}

}  // namespace statusbar::avtp
