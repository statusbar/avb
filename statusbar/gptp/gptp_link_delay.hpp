#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

//
// gPTP peer-link delay computation and neighbor rate ratio math.
//
// Pure functions; no state. Used by MDPdelayReq during each 4-timestamp
// Pdelay exchange to produce meanLinkDelay and neighborRateRatio for
// the follower's servo pipeline.
//
// Spec: IEEE 802.1AS-2020 Clause 11.2.19 and Clause 8.1 (neighbor
// rate ratio semantics).
//
// Reference implementation: OpenAvnu ptp_message.cpp lines 1746-1816.
//

#include <cstdint>

namespace statusbar::gptp {

/// One complete 4-timestamp Pdelay exchange, as captured by the
/// requestor (local) side.
///
/// Timestamps are all nanoseconds in the local clock timebase,
/// already adjusted for PHY delay compensation by the caller.
///
/// Naming follows the 802.1AS spec convention:
///   t1 = Pdelay_Req TX at requestor (our local HW tx timestamp)
///   t2 = Pdelay_Req RX at responder (peer's HW rx, from Pdelay_Resp body)
///   t3 = Pdelay_Resp TX at responder (peer's HW tx, from Pdelay_Resp_FUp body)
///   t4 = Pdelay_Resp RX at requestor (our local HW rx timestamp)
struct PdelayExchange
{
    int64_t t1_req_tx_local_ns{0};
    int64_t t2_req_rx_peer_ns{0};
    int64_t t3_resp_tx_peer_ns{0};
    int64_t t4_resp_rx_local_ns{0};
};

/// Compute meanLinkDelay from a single Pdelay exchange.
///
/// Formula: meanLinkDelay = ((t4 - t1) - neighborRateRatio * (t3 - t2)) / 2
///
/// The `neighbor_rate_ratio` argument is the current best estimate of
/// (local_freq / neighbor_freq). On the very first exchange the
/// caller passes 1.0 (bootstrap); subsequent exchanges use the value
/// returned by `compute_neighbor_rate_ratio`.
///
/// Returns the link delay in local-clock nanoseconds.
///
/// See IEEE 802.1AS-2020 Clause 11.2.19.3.4.
[[nodiscard]] auto compute_link_delay_ns(PdelayExchange const& ex, double neighbor_rate_ratio) noexcept -> int64_t;

/// Compute neighborRateRatio from two successive Pdelay exchanges.
///
/// Semantics per 802.1AS-2020 Clause 8.1: the returned value is the
/// ratio of the local clock's frequency to the neighbor's clock
/// frequency. Values > 1.0 mean the local clock is faster than the
/// neighbor.
///
/// Formula: neighborRateRatio = (t1_curr - t1_prev) / (t2_curr - t2_prev)
///
/// Both intervals span the same real-time window to first order
/// (ignoring link-delay jitter between exchanges). This is the
/// simple form; for stable point-to-point links it is accurate to
/// well within the servo's ±ppm tolerance.
///
/// Returns 1.0 if `curr` and `prev` have the same `t2_req_rx_peer_ns`
/// (would otherwise divide by zero).
[[nodiscard]] auto compute_neighbor_rate_ratio(PdelayExchange const& prev, PdelayExchange const& curr) noexcept -> double;

/// Clamp a computed neighborRateRatio to ±ppm from 1.0.
///
/// OpenAvnu clamps to ±250 ppm by default. Rate offsets outside this
/// range are likely a measurement error or a peer running a
/// non-compliant clock; discarding them avoids servo runaway.
///
/// The argument is the rate ratio (1.0 = perfectly matched).
/// Returns the clamped ratio.
[[nodiscard]] auto clamp_rate_ratio_ppm(double ratio, double max_ppm) noexcept -> double;

}  // namespace statusbar::gptp
