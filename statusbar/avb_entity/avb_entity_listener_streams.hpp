#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// @file avb_entity_listener_streams.hpp
/// @brief ListenerStreams — the local AVB stream RX path, extracted from
/// AvbEntityAudioIO (god-object phase 3, RX half).
///
/// Owns the two stream-input deserialize contexts (AM824 + AAF), the RX packet
/// counters, the IEEE 1722.1 STREAM_INPUT health counters, and the borrowed RX
/// socket handle, plus the methods that decode one received stream frame, gate it
/// to the connected stream, update the counters, and hand the accepted audio to a
/// StreamRxAudioSink. Runs on the reactor/RX thread (on_stream_rx_frame is invoked
/// by the entity's StreamRxHandler; the ACMP connect/disconnect methods are wired
/// as the acmp_listener connection callbacks).
///
/// Reads the entity's config / gPTP-now through references and the ACMP/MSRP state
/// through a NanoAvbComponents reference (all bound at construction, same names as
/// the entity's members, so the moved method bodies are unchanged). It knows
/// nothing about where the audio goes -- only the StreamRxAudioSink.

#include "statusbar/avb_entity/avb_entity_audio_io_config.hpp"
#include "statusbar/avb_entity/avb_entity_stream_counters.hpp"
#include "statusbar/avb_entity/avb_entity_stream_rx_sink.hpp"
#include "statusbar/avtp/avtp_aaf_stream_input.hpp"
#include "statusbar/avtp/avtp_am824_stream_input.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/itc/itc_telemetry_counter.hpp"
#include "statusbar/nanoavb/nanoavb_components.hpp"
#include "statusbar/net/net_rawnet.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace statusbar::avb_entity {

struct ListenerStreams
{
    ListenerStreams(
        AvbEntityAudioIOConfig const& config,
        nanoavb::NanoAvbComponents& components,
        std::atomic<uint64_t> const& last_gptp_ns,
        StreamRxAudioSink* audio_sink) noexcept
        : config_{config}
        , components_{components}
        , last_gptp_ns_{last_gptp_ns}
        , audio_sink_{audio_sink}
    {}

    static constexpr uint32_t SAMPLE_RATE = 96000;
    static constexpr uint16_t AM824_STREAM_INDEX = 0;
    static constexpr uint16_t AAF_STREAM_INDEX = 1;

    /// Decode one received AVTP stream frame, dispatch by subtype to the AM824 or
    /// AAF deserializer, update counters, and offer the accepted audio to the sink.
    /// Reactor/RX thread.
    void on_stream_rx_frame(std::span<uint8_t const> frame, int64_t now_ns);

    /// True if @p frame belongs to the stream currently connected to STREAM_INPUT
    /// @p stream_index (listener connected + stream_id at offset 4 matches).
    [[nodiscard]] auto frame_is_for_listener(uint16_t stream_index, std::span<uint8_t const> frame) const -> bool;

    /// Update STREAM_INPUT counters for one received packet (delegates to the pure,
    /// unit-tested tally_stream_input_packet). See the entity-level doc for ts_sparse.
    void update_stream_input_counters(
        uint16_t stream_index,
        uint8_t seq,
        uint32_t avtp_ts,
        bool tv,
        bool tu,
        bool mr,
        bool format_ok,
        uint64_t samples_per_ch,
        bool ts_sparse);

    /// Fill the GET_COUNTERS bitmap + values for a STREAM_INPUT index (true if it is
    /// one of our stream inputs). Control plane (AEM handler).
    [[nodiscard]] auto fill_stream_input_counters(uint16_t descriptor_index, uint32_t& valid, std::array<uint32_t, 32>& out) const
        -> bool;

    /// Our listener connected to a remote talker: reserve via MSRP (Listener Ready)
    /// and join the talker's stream multicast group on the RX socket. Wired as the
    /// acmp_listener connect callback. Reactor thread.
    void on_listener_connected(uint16_t stream_index, ieee::Eui64 const& stream_id, ieee::Eui48 dest_mac);

    /// Our listener disconnected: withdraw the MSRP reservation and leave the
    /// talker's multicast group. Wired as the acmp_listener disconnect callback.
    void on_listener_disconnected(uint16_t stream_index);

    // --- References / collaborators (bound at construction) --------------------
    AvbEntityAudioIOConfig const& config_;
    nanoavb::NanoAvbComponents& components_;  ///< for acmp_listener (connection state) + msrp_handler (reservation)
    std::atomic<uint64_t> const& last_gptp_ns_;
    /// Where accepted listener audio goes (today the WAN tunnel). The listener does
    /// not know what the sink does with it. Nullable.
    StreamRxAudioSink* audio_sink_{nullptr};
    /// Non-owning handle to the StreamRxHandler's RX socket (owned by the reactor).
    /// Set by the entity in start() before the handler is moved into the reactor;
    /// used to join/leave a remote talker's stream multicast group on connect/disconnect.
    net::RawnetContext* rx_sock_{nullptr};

    // --- Owned RX state --------------------------------------------------------
    /// Stream 0: AM824 deserialize context. Stream 1: AAF deserialize context.
    std::optional<avtp::Am824StreamInputContext> am824_in_{};
    std::optional<avtp::AafStreamInputContext> aaf_in_{};

    /// Per-format data-plane counters (written on the reactor thread, read for status).
    itc::TelemetryCounter<uint64_t> am824_rx_packets_{};
    itc::TelemetryCounter<uint64_t> am824_rx_samples_{};
    itc::TelemetryCounter<uint64_t> am824_rx_bad_{};
    itc::TelemetryCounter<uint64_t> aaf_rx_packets_{};
    itc::TelemetryCounter<uint64_t> aaf_rx_samples_{};
    itc::TelemetryCounter<uint64_t> aaf_rx_bad_{};

    /// IEEE 1722.1 STREAM_INPUT health counters (Clause 7.4.42), [0]=AM824, [1]=AAF.
    /// The per-packet update lives in avb_entity_stream_counters.hpp (unit-tested).
    std::array<StreamInputCounters, 2> stream_in_counters_{};
};

}  // namespace statusbar::avb_entity
