// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_stream_common.hpp"

namespace statusbar::avtp {

auto reconstruct_avtp_timestamp(uint32_t avtp_timestamp, uint64_t gptp_now_ns) noexcept -> uint64_t
{
    // The AVTP timestamp carries the lower 32 bits of the gPTP time in nanoseconds.
    // We reconstruct the full 64-bit value by taking the upper 32 bits from the
    // approximate receive time and the lower 32 bits from the AVTP field.
    //
    // The presentation time is typically slightly ahead of "now" (within a few ms),
    // so we pick the reconstruction closest to gptp_now_ns.
    uint64_t const upper = gptp_now_ns & 0xFFFF'FFFF'0000'0000ULL;
    uint64_t const candidate = upper | avtp_timestamp;

    // Check if wrapping the upper bits by +1 or -1 gives a closer result.
    constexpr uint64_t wrap = 0x1'0000'0000ULL;  // 2^32

    uint64_t best = candidate;
    int64_t best_dist = static_cast<int64_t>(candidate) - static_cast<int64_t>(gptp_now_ns);
    if (best_dist < 0) {
        best_dist = -best_dist;
    }

    // Try candidate + 2^32 (avtp_timestamp is in the next upper-32-bit epoch)
    if (candidate + wrap > candidate) {  // overflow guard
        int64_t const dist_plus = static_cast<int64_t>(candidate + wrap) - static_cast<int64_t>(gptp_now_ns);
        int64_t const abs_dist_plus = dist_plus < 0 ? -dist_plus : dist_plus;
        if (abs_dist_plus < best_dist) {
            best = candidate + wrap;
            best_dist = abs_dist_plus;
        }
    }

    // Try candidate - 2^32 (avtp_timestamp is in the previous upper-32-bit epoch)
    if (candidate >= wrap) {
        int64_t const dist_minus = static_cast<int64_t>(candidate - wrap) - static_cast<int64_t>(gptp_now_ns);
        int64_t const abs_dist_minus = dist_minus < 0 ? -dist_minus : dist_minus;
        if (abs_dist_minus < best_dist) {
            best = candidate - wrap;
        }
    }

    return best;
}

}  // namespace statusbar::avtp
