#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// @file avb_entity_talker_gate.hpp
/// @brief TalkerGate — the per-stream "should this talker transmit?" gate,
/// extracted from AvbEntityAudioIO (god-object phase 4).
///
/// Spec-correct SR-class admission: a talker puts a stream on the wire only when
/// BOTH an ACMP connection exists AND the downstream listener permits transmit via
/// MSRP Listener Ready, the latter held across an MRP LeaveAll re-registration blip
/// by a grace window. The gate is cross-thread: the MSRP listener-ready state is
/// written by the reactor thread (note_listener_ready) and read by the media-timer
/// thread (should_transmit), so the readiness flags + last-ready timestamps are
/// atomic. Reads ACMP connection state + the gate-enable flag through references
/// bound at construction.

#include "statusbar/avb_entity/avb_entity_audio_io_config.hpp"
#include "statusbar/nanoavb/nanoavb_components.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace statusbar::avb_entity {

struct TalkerGate
{
    TalkerGate(AvbEntityAudioIOConfig const& config, nanoavb::NanoAvbComponents const& components) noexcept
        : config_{config}
        , components_{components}
    {}

    /// One per talker stream: 0=AM824, 1=AAF, 2=CRF.
    static constexpr size_t STREAM_COUNT = 3;
    /// Grace window the MSRP Listener Ready is held across a LeaveAll re-registration
    /// blip (Ready momentarily withdrawn for ~1-3 s on the ~10 s leave-all cycle), so
    /// a normal MRP cycle does NOT chop the stream. A genuine listener departure (no
    /// re-declare for GRACE) closes the gate.
    static constexpr int64_t GRACE_NS = 5'000'000'000;

    /// Reactor thread: an MSRP Listener Ready declaration for @p stream_id changed
    /// to @p ready. Matched to a talker stream index by ACMP stream_id (same value
    /// MSRP advertises); stamps the readiness flag + last-ready time.
    void note_listener_ready(nanoavb::StreamId const& stream_id, bool ready);

    /// Media-timer thread: may talker stream @p idx put its stream on the wire at
    /// steady-clock time @p now_ns? True if gating is disabled, or an ACMP
    /// connection exists AND MSRP Listener Ready is set (or within GRACE_NS).
    [[nodiscard]] auto should_transmit(uint16_t idx, int64_t now_ns) const noexcept -> bool;

    // --- References (bound at construction) ------------------------------------
    AvbEntityAudioIOConfig const& config_;
    nanoavb::NanoAvbComponents const& components_;  ///< reads acmp_talker connection state

    // --- Owned gate state (cross-thread) ---------------------------------------
    /// "A listener permits transmit" (MSRP Listener Ready), per stream. Written by
    /// note_listener_ready (reactor), read by should_transmit (media timer).
    std::array<std::atomic<bool>, STREAM_COUNT> listener_ready_{};
    /// Last steady-clock time (ns) each stream's Listener Ready was observed true;
    /// the strict gate keeps transmitting for GRACE_NS past this.
    std::array<std::atomic<int64_t>, STREAM_COUNT> ready_ns_{};
};

}  // namespace statusbar::avb_entity
