#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for AVTP CrfV1Pdu. Split from avtp_crf_v1.hpp
/// so consumers that only need the data structures do not pay the
/// compile-time cost of <format>.

#include "statusbar/avtp/avtp_crf.hpp"
#include "statusbar/avtp/avtp_crf_format.hpp"
#include "statusbar/avtp/avtp_crf_v1.hpp"
#include "statusbar/tsn/tsn_clock_identity_format.hpp"
#include "statusbar/tsn/tsn_stream_id_format.hpp"

#include <format>

namespace statusbar::avtp {

/// Format a CrfV1Pdu to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, CrfV1Pdu const& pdu) -> OutputIt
{
    out = std::format_to(out, "CRF(v1): sv={} mr={} fs={} tu={}", pdu.sv(), pdu.mr(), pdu.fs(), pdu.tu());
    out = std::format_to(out, " seq={}", pdu.get_sequence_num());
    out = std::format_to(out, " stream_id=");
    out = tsn::format_to(out, pdu.stream_id());
    out = std::format_to(out, " gm=");
    out = tsn::format_to(out, pdu.get_ptp_grandmaster_identity());
    out = std::format_to(
        out,
        " type={} pull={} base_freq={} ts_count={} ts_interval={}",
        crf_type_name(pdu.get_type()),
        crf_pull_name(pdu.pull()),
        pdu.base_frequency(),
        pdu.timestamp_count(),
        pdu.timestamp_interval());
    return out;
}

}  // namespace statusbar::avtp
