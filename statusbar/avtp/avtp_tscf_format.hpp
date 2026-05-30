#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for AVTP TscfPdu. Split from avtp_tscf.hpp
/// so consumers that only need the data structures do not pay the
/// compile-time cost of <format>.

#include "statusbar/avtp/avtp_tscf.hpp"
#include "statusbar/tsn/tsn_stream_id_format.hpp"

#include <format>

namespace statusbar::avtp {

/// Format a TscfPdu to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, TscfPdu const& pdu) -> OutputIt
{
    out = std::format_to(out, "TSCF: sv={} mr={} tv={} tu={}", pdu.sv(), pdu.mr(), pdu.tv(), pdu.tu());
    out = std::format_to(out, " seq={}", pdu.get_sequence_num());
    out = std::format_to(out, " stream_id=");
    out = tsn::format_to(out, pdu.stream_id());
    if (pdu.tv()) {
        out = std::format_to(out, " timestamp={}", pdu.get_avtp_timestamp());
    }
    out = std::format_to(out, " stream_data_length={}", pdu.get_stream_data_length());
    return out;
}

}  // namespace statusbar::avtp
