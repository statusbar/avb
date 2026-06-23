#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// @file avb_entity_udptun_telemetry.hpp
/// @brief Inter-site tunnel telemetry as a standalone, separately-shareable struct.

#include "statusbar/itc/itc_published.hpp"
#include "statusbar/itc/itc_telemetry_counter.hpp"

#include <cstdint>

namespace statusbar::avb_entity {

/// Lock-free inter-thread telemetry for the inter-site tunnel. Producers run on
/// the RX / media-timer / punch-worker threads; readers are print_state and the
/// punch-service watchdog. Kept as its own struct (not scattered members of
/// AvbEntityAudioIO) so it can be owned/shared independently of the entity —
/// e.g. held by shared_ptr and handed to the EntityUdptunBridge once the tunnel
/// data plane is extracted, without coupling the counters to the entity's
/// storage. itc primitives (preferred over bare std::atomic) give wait-free
/// producer add()/publish() and acquire-ordered reader load().
struct UdptunTelemetry
{
    itc::TelemetryCounter<uint64_t> tx_packets{};            ///< primary tunnel datagrams sent
    itc::TelemetryCounter<uint64_t> rx_packets{};            ///< decoded tunnel-audio datagrams received
    itc::TelemetryCounter<uint64_t> any_rx{};                ///< ALL datagrams received (audio + keepalive)
    itc::TelemetryCounter<uint64_t> egress_reset_count{};    ///< egress anchor resets (watchdog)
    itc::TelemetryCounter<uint64_t> egress_repunch_count{};  ///< media-thread re-punches (escalation + stall)
    itc::Published<int64_t> last_real_ingest_tai{};          ///< last real tunnel-audio ingest TAI (ns)
};

}  // namespace statusbar::avb_entity
