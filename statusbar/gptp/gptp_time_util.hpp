#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Shared gPTP time utilities used by multiple translation units.

#include "statusbar/gptp/gptp_base.hpp"

#include <cstdint>

namespace statusbar::gptp {

/// Convert a gptp::Timestamp (6 byte seconds + 4 byte nanos) to a
/// signed int64 nanoseconds value. Only differences between timestamps
/// matter for rate ratio math. For realistic PTP timestamps (post-2000
/// TAI) this is ~2^30 seconds x 10^9 ≈ 2^60 ns, well within int64 range.
inline auto ts_to_ns(Timestamp const& ts) noexcept -> int64_t
{
    int64_t const secs = static_cast<int64_t>(ts.seconds());
    int64_t const ns = static_cast<int64_t>(ts.nanos());
    return (secs * 1'000'000'000LL) + ns;
}

}  // namespace statusbar::gptp
