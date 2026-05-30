#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for AVTP AafPdu. Split from avtp_aaf.hpp so
/// consumers that only need the data structures do not pay the
/// compile-time cost of <format>.

#include "statusbar/avtp/avtp_aaf.hpp"
#include "statusbar/tsn/tsn_stream_id_format.hpp"

#include <format>

namespace statusbar::avtp {

/// Format an AafPdu to an output iterator
/// @param out The output iterator to write formatted text to
/// @param pdu The AAF PDU to format
template <typename OutputIt>
auto format_to(OutputIt out, AafPdu const& pdu) -> OutputIt
{
    out = std::format_to(out, "AAF: stream_id=");
    out = tsn::format_to(out, pdu.stream_id());
    out = std::format_to(out, " seq={}", pdu.get_sequence_num());
    out = std::format_to(
        out,
        "\n     format={} rate={} channels={} samples={}",
        aaf_format_name(pdu.get_format()),
        aaf_sample_rate_name(pdu.nsr()),
        pdu.channels_per_frame(),
        pdu.sample_count());
    out = std::format_to(out, "\n     bit_depth={} timestamp={}", pdu.get_bit_depth(), pdu.get_avtp_timestamp());
    if (pdu.sp()) {
        out = std::format_to(out, " (sparse)");
    }
    return out;
}

}  // namespace statusbar::avtp
