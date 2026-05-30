#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for AVTP NtscfPdu. Split from avtp_ntscf.hpp
/// so consumers that only need the data structures do not pay the
/// compile-time cost of <format>.

#include "statusbar/avtp/avtp_ntscf.hpp"
#include "statusbar/tsn/tsn_stream_id_format.hpp"

#include <format>

namespace statusbar::avtp {

/// Format an NtscfPdu to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, NtscfPdu const& pdu) -> OutputIt
{
    out = std::format_to(out, "NTSCF: sv={}", pdu.sv());
    out = std::format_to(out, " seq_lsb={}", pdu.get_sequence_num_lsb());
    out = std::format_to(out, " stream_id=");
    out = tsn::format_to(out, pdu.stream_id());
    out = std::format_to(out, " ntscf_data_length={}", pdu.ntscf_data_length());
    return out;
}

}  // namespace statusbar::avtp
