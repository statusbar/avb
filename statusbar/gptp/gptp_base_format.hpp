#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for the core gPTP base types. Split out from
/// gptp_base.hpp so consumers that only need the data structures do not
/// pay the compile-time cost of <format>.

#include "statusbar/gptp/gptp_base.hpp"
#include "statusbar/tsn/tsn_clock_identity_format.hpp"

#include <format>

namespace statusbar::gptp {

/// Re-export the tsn::format_to function so gPTP format_to overloads can
/// find ClockIdentity formatting via unqualified lookup.
using tsn::format_to;

/// Format a SourcePortIdentity to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, SourcePortIdentity const& spi) -> OutputIt
{
    out = format_to(out, spi.clock_identity);
    return std::format_to(out, " port {}", spi.port_number.get());
}

/// Format a Timestamp to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, Timestamp const& ts) -> OutputIt
{
    return std::format_to(out, "{}.{:09}", ts.seconds(), ts.nanos());
}

/// Format a ClockQuality to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, ClockQuality const& cq) -> OutputIt
{
    return std::format_to(
        out,
        "class={} accuracy={:#04x} variance={:#06x}",
        cq.clock_class.get(),
        cq.clock_accuracy.get(),
        cq.offset_scaled_log_variance.get());
}

}  // namespace statusbar::gptp
