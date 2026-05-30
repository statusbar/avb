#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Shared AVTP stream utilities — timestamp reconstruction common to AM824 and AAF
///
/// The AVTP timestamp field (IEEE 1722-2016 Section 4.4.4) carries only the lower
/// 32 bits of the gPTP time in nanoseconds. Both AM824 and AAF streams need to
/// reconstruct the full 64-bit gPTP-domain value using an approximate receive time.

#include <cstdint>

namespace statusbar::avtp {

/// Reconstruct a full 64-bit gPTP timestamp from a 32-bit AVTP timestamp
/// and an approximate current gPTP time.
///
/// The AVTP timestamp wraps every ~4.295 seconds. To reconstruct the full
/// 64-bit value, we combine the upper 32 bits from the approximate receive
/// time with the lower 32 bits from the AVTP field, selecting the candidate
/// closest to gptp_now_ns to handle wrap-around in both directions.
///
/// @param avtp_timestamp The 32-bit AVTP presentation timestamp from the AVTPDU
/// @param gptp_now_ns    Approximate gPTP time when the packet was received (full 64-bit)
/// @return Full 64-bit gPTP-domain presentation timestamp
[[nodiscard]] auto reconstruct_avtp_timestamp(uint32_t avtp_timestamp, uint64_t gptp_now_ns) noexcept -> uint64_t;

}  // namespace statusbar::avtp
