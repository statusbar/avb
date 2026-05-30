#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for AVTP EecfPdu. Split from avtp_eecf.hpp
/// so consumers that only need the data structures do not pay the
/// compile-time cost of <format>.

#include "statusbar/avtp/avtp_eecf.hpp"
#include "statusbar/ieee/ieee_ethernet_format.hpp"

#include <format>

namespace statusbar::avtp {

/// Format an EecfPdu to an output iterator
/// @param out The output iterator to write formatted text to
/// @param pdu The EECF PDU to format
template <typename OutputIt>
auto format_to(OutputIt out, EecfPdu const& pdu) -> OutputIt
{
    out = std::format_to(out, "EECF: enc={}", eecf_enc_mode_name(pdu.enc()));
    out = std::format_to(out, " key_id=");
    out = ieee::format_to(out, pdu.key_id());
    out = std::format_to(out, " encrypted_payload_length={}", pdu.encrypted_payload_length());
    return out;
}

}  // namespace statusbar::avtp
