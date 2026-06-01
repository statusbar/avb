#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_per_source_tracker.hpp"
#include "statusbar/udptun/udptun_stats.hpp"

#include <ostream>
#include <string_view>

namespace statusbar::udptun {

/// RTT loss accounting inputs. The caller computes both fields with whatever
/// grace policy it wants; the report just formats them.
///
/// `sent_total` is total packets sent so far (the displayed `sent=N`).
///
/// `sent_at_cutoff` is how many packets had been sent at `now - grace`,
/// i.e. excluding the most recent ones still inside an RTT window. Loss is
/// computed as `max(0, sent_at_cutoff - received) / sent_at_cutoff`. When the
/// caller doesn't want a grace window (e.g. the final summary after an RX
/// drain), it passes `sent_at_cutoff == sent_total`.
struct TxLossStats
{
    uint64_t sent_total{0};
    uint64_t sent_at_cutoff{0};
    /// sendto() returned -1 on this many primary or redundant emissions
    /// (e.g. ENOBUFS). Surfaces TX-side packet drops separately from
    /// RX-side loss so the operator can tell "the wire ate it" from
    /// "the kernel never queued it".
    uint64_t tx_failures{0};
};

/// Optional redundancy counters from RedundantRxTracker. When present,
/// the RTT line gains `recovered=R true_loss=Y% (T)` describing how many
/// primary-stream losses were rescued by the redundant copy and how many
/// were lost on both copies. `primary_received` is used to compute the
/// `missing` count so it reflects primary loss specifically (i.e. drops
/// the user would see if there were no redundant fallback) even though
/// `LatencyStats.time_stats.count` now also includes rescued samples.
struct RedundancyDisplayStats
{
    uint64_t recovered{0};
    uint64_t true_loss{0};
    uint64_t primary_received{0};
};

/// Print one summary line per in-use source to `out`, prefixed with
/// `wall_seconds`. If `rtt` is non-null and has at least one sample,
/// also prints a one-line RTT summary. When `tx` is non-null and
/// `sent_total > 0`, the RTT line includes
/// `sent=N loss=X.XX% (N missing sequence ids)`. When `red` is non-null,
/// the RTT line additionally reports `recovered=R true_loss=Y% (T)`.
/// When `gptp_ns` is non-zero, it is appended to each per-source line
/// as `gptp_ns=NNN` so an operator can correlate a high-latency event
/// observed on one device with the same instant on the peer.
/// Cumulative values.
void print_live_report(
    std::ostream& out,
    double wall_seconds,
    PerSourceTracker const& tracker,
    LatencyStats const* rtt = nullptr,
    TxLossStats const* tx = nullptr,
    RedundancyDisplayStats const* red = nullptr,
    int64_t gptp_ns = 0);

/// End-of-run summary: per-source line + full ASCII histogram + global
/// dropped_invalid. `label` is used as the heading prefix
/// ("=== <label> final summary ==="). `gptp_ns` is forwarded to the
/// per-source line if non-zero.
void print_final_summary(
    std::ostream& out,
    std::string_view label,
    PerSourceTracker const& tracker,
    LatencyStats const* rtt = nullptr,
    TxLossStats const* tx = nullptr,
    RedundancyDisplayStats const* red = nullptr,
    int64_t gptp_ns = 0);

}  // namespace statusbar::udptun
