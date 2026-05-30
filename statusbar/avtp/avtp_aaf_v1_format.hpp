#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for AVTP AafV1Pdu. Split from avtp_aaf_v1.hpp
/// so consumers that only need the data structures do not pay the
/// compile-time cost of <format>.

#include "statusbar/avtp/avtp_aaf.hpp"
#include "statusbar/avtp/avtp_aaf_format.hpp"
#include "statusbar/avtp/avtp_aaf_v1.hpp"
#include "statusbar/tsn/tsn_clock_identity_format.hpp"
#include "statusbar/tsn/tsn_stream_id_format.hpp"

#include <format>

namespace statusbar::avtp {

/// Format an AafV1Pdu to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, AafV1Pdu const& pdu) -> OutputIt
{
    out = std::format_to(out, "AAF(v1): sv={} mr={} tv={} tu={}", pdu.sv(), pdu.mr(), pdu.tv(), pdu.tu());
    out = std::format_to(out, " seq={}", pdu.get_sequence_num());
    out = std::format_to(out, " stream_id=");
    out = tsn::format_to(out, pdu.stream_id());
    if (pdu.tv()) {
        out = std::format_to(out, " timestamp={}", pdu.get_avtp_timestamp());
        out = std::format_to(out, " gm=");
        out = tsn::format_to(out, pdu.get_ptp_grandmaster_identity());
    }
    out = std::format_to(
        out,
        " fmt={} nsr={} ch={} depth={} len={}",
        aaf_format_name(pdu.get_format()),
        aaf_sample_rate_name(pdu.nsr()),
        pdu.channels_per_frame(),
        pdu.get_bit_depth(),
        pdu.get_stream_data_length());
    if (pdu.sp()) {
        out = std::format_to(out, " sp");
    }
    return out;
}

}  // namespace statusbar::avtp
