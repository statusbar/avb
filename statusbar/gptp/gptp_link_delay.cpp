// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/gptp/gptp_link_delay.hpp"

#include <algorithm>
#include <cmath>

namespace statusbar::gptp {

auto compute_link_delay_ns(PdelayExchange const& ex, double neighbor_rate_ratio) noexcept -> int64_t
{
    // ((t4 - t1) - neighborRateRatio * (t3 - t2)) / 2
    int64_t const local_round_trip_ns = ex.t4_resp_rx_local_ns - ex.t1_req_tx_local_ns;
    int64_t const peer_delta_ns = ex.t3_resp_tx_peer_ns - ex.t2_req_rx_peer_ns;
    double const peer_delta_in_local = static_cast<double>(peer_delta_ns) * neighbor_rate_ratio;
    double const delay = (static_cast<double>(local_round_trip_ns) - peer_delta_in_local) / 2.0;
    // Round to nearest integer ns.
    return static_cast<int64_t>(std::llround(delay));
}

auto compute_neighbor_rate_ratio(PdelayExchange const& prev, PdelayExchange const& curr) noexcept -> double
{
    int64_t const mine_elapsed_ns = curr.t1_req_tx_local_ns - prev.t1_req_tx_local_ns;
    int64_t const theirs_elapsed_ns = curr.t2_req_rx_peer_ns - prev.t2_req_rx_peer_ns;
    if (theirs_elapsed_ns == 0) {
        return 1.0;  // degenerate case; preserve bootstrap value
    }
    return static_cast<double>(mine_elapsed_ns) / static_cast<double>(theirs_elapsed_ns);
}

auto clamp_rate_ratio_ppm(double ratio, double max_ppm) noexcept -> double
{
    double const max_delta = max_ppm * 1e-6;
    double const lower = 1.0 - max_delta;
    double const upper = 1.0 + max_delta;
    return std::clamp(ratio, lower, upper);
}

}  // namespace statusbar::gptp
