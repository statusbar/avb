#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for AVTP IpAvtpduHeader. Split from
/// avtp_ip_encap.hpp so consumers that only need the data structures
/// do not pay the compile-time cost of <format>.

#include "statusbar/avtp/avtp_ip_encap.hpp"

#include <format>

namespace statusbar::avtp {

/// Format an IpAvtpduHeader to an output iterator
/// @param out The output iterator to write formatted text to
/// @param header The IP AVTPDU header to format
template <typename OutputIt>
auto format_to(OutputIt out, IpAvtpduHeader const& header) -> OutputIt
{
    return std::format_to(out, "IP-AVTPDU: seq={}", header.encapsulation_sequence_num());
}

}  // namespace statusbar::avtp
