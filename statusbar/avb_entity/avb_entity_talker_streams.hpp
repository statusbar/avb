#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// @file avb_entity_talker_streams.hpp
/// @brief TalkerStreams — the local AVB stream TX path, extracted from
/// AvbEntityAudioIO (god-object phase 3).
///
/// Owns the qdisc-bypass TX socket, the AM824/AAF/CRF stream serializers, the
/// destination MACs, the TX packet counters, and the TX-capture recorder, plus
/// the three transmit_* methods that serialize one packet and put it on the
/// wire. Runs on the SCHED_FIFO media-timer thread (called from
/// AvbEntityAudioIO::process_audio). Reads the entity's config / media clock /
/// audio buffer / gPTP-now through references bound at construction (same names
/// as the entity's members, so the moved method bodies are unchanged).

#include "statusbar/avb_entity/avb_entity_audio_io_config.hpp"
#include "statusbar/avb_entity/tx_pcap_recorder.hpp"
#include "statusbar/avtp/avtp_aaf_stream_output.hpp"
#include "statusbar/avtp/avtp_am824_stream_output.hpp"
#include "statusbar/avtp/avtp_crf_stream_output.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/net/net_rawnet.hpp"
#include "statusbar/ptpclient/ptpclient_media_clock.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <optional>
#include <span>

namespace statusbar::avb_entity {

struct TalkerStreams
{
    TalkerStreams(
        AvbEntityAudioIOConfig const& config,
        ptpclient::MediaClockGenerator const& media_clock,
        std::pmr::vector<float>& audio_buffer,
        size_t const& channels,
        std::atomic<uint64_t> const& last_gptp_ns) noexcept
        : config_{config}
        , media_clock_{media_clock}
        , audio_buffer_{audio_buffer}
        , channels_{channels}
        , last_gptp_ns_{last_gptp_ns}
    {}

    /// Serialize + send one packet of each stream. Media-timer (SCHED_FIFO) thread.
    void transmit_am824(uint64_t now_ns, uint32_t samples);
    void transmit_aaf(uint64_t now_ns, uint16_t samples, std::span<float const> src);
    void transmit_crf();

    static constexpr uint32_t SAMPLE_RATE = 96000;

    // References into the owning entity (bound at construction).
    AvbEntityAudioIOConfig const& config_;
    ptpclient::MediaClockGenerator const& media_clock_;
    std::pmr::vector<float>& audio_buffer_;
    size_t const& channels_;
    std::atomic<uint64_t> const& last_gptp_ns_;

    // Owned TX state.
    net::RawnetContext stream_tx_{};
    std::optional<avtp::Am824StreamOutputContext> am824_out_{};
    ieee::Eui48 am824_dest_mac_{};
    std::optional<avtp::AafStreamOutputContext> aaf_out_{};
    ieee::Eui48 aaf_dest_mac_{};
    std::optional<avtp::CrfStreamOutputContext> crf_out_{};
    ieee::Eui48 crf_dest_mac_{};
    uint64_t crf_event_{0};  ///< media-clock event index for the next CRF timestamp
    uint64_t am824_tx_packets_{0};
    uint64_t aaf_tx_packets_{0};
    /// TX stream capture (diagnostic). last_tx_gptp_ns_ is set just before each
    /// send so the socket egress tap can stamp the captured frame with the gPTP TX time.
    TxPcapRecorder tx_pcap_recorder_{};
    uint64_t last_tx_gptp_ns_{0};
};

}  // namespace statusbar::avb_entity
