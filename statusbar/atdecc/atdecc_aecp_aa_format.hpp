#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for ATDECC AECP Address Access types. Split from
/// atdecc_aecp_aa.hpp so consumers that only need the data structures do not
/// pay the compile-time cost of <format>.

#include "statusbar/atdecc/atdecc_aecp_aa.hpp"
#include "statusbar/atdecc/atdecc_aecp_format.hpp"

#include <format>

namespace statusbar::atdecc {

/// Format an AecpAaDu header to an output iterator
template <typename OutputIt>
auto format_to(OutputIt out, AecpAaDu const& pdu) -> OutputIt
{
    out = atdecc::format_to(out, pdu.common);
    out = std::format_to(out, "\n        AA: tlv_count={}", pdu.tlv_count.get());
    if (pdu.common.is_response()) {
        out = std::format_to(out, " status={} ({})", pdu.common.status(), aa_status_name(pdu.common.status()));
    }
    return out;
}

}  // namespace statusbar::atdecc
