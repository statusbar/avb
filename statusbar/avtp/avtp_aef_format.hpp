#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for AVTP AEF (AefContinuousPdu, AefDiscretePdu).
/// Split from avtp_aef.hpp so consumers that only need the data
/// structures do not pay the compile-time cost of <format>.

#include "statusbar/avtp/avtp_aef.hpp"
#include "statusbar/ieee/ieee_ethernet_format.hpp"

#include <format>

namespace statusbar::avtp {

/// Format an AefContinuousPdu to an output iterator
/// @param out The output iterator to write formatted text to
/// @param pdu The AEF continuous PDU to format
template <typename OutputIt>
auto format_to(OutputIt out, AefContinuousPdu const& pdu) -> OutputIt
{
    out = std::format_to(out, "AEF-C: enc={}", aef_enc_mode_name(pdu.enc()));
    out = std::format_to(out, " key_id=");
    out = ieee::format_to(out, pdu.key_id());
    out = std::format_to(out, " stream_data_length={}", pdu.get_stream_data_length());
    return out;
}

/// Format an AefDiscretePdu to an output iterator
/// @param out The output iterator to write formatted text to
/// @param pdu The AEF discrete PDU to format
template <typename OutputIt>
auto format_to(OutputIt out, AefDiscretePdu const& pdu) -> OutputIt
{
    out = std::format_to(out, "AEF-D: enc={}", aef_enc_mode_name(pdu.enc()));
    out = std::format_to(out, " key_id=");
    out = ieee::format_to(out, pdu.key_id());
    out = std::format_to(out, " control_data_length={}", pdu.control_data_length());
    return out;
}

}  // namespace statusbar::avtp
