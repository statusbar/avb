#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// @file avb_entity_stream_rx_sink.hpp
/// @brief StreamRxAudioSink — where a listener delivers received stream audio.
///
/// The AVB listener's job is to receive a connected talker's stream, validate
/// it, and hand the accepted audio payload to *something*. Today that something
/// is the inter-site WAN tunnel (EntityUdptunBridge), but it could equally be a
/// local audio device, a file recorder, or a DSP graph. This interface is the
/// seam that keeps the listener from depending on any one of those concretely:
/// the listener calls on_listener_audio() for every accepted packet and the sink
/// decides what to do with it (including dropping it -- e.g. the tunnel ignores
/// audio for a stream it is not configured to source).

#include <cstdint>
#include <span>

namespace statusbar::avb_entity {

/// Wire format of the payload bytes handed to the sink. The sink may need to
/// transcode: AM824 carries 61883-6 MBLA quadlets ([0x40 label][24-bit]); AAF
/// carries interleaved int32 PCM directly.
enum class StreamAudioFormat : uint8_t
{
    am824_mbla,  ///< IEC 61883-6 MBLA quadlets (needs MBLA->int32 transcode for int32 sinks)
    aaf_int32,   ///< AAF interleaved int32 PCM (already linear)
};

/// Consumes the audio payload of one accepted listener stream packet. Called on
/// the reactor/RX thread (the same thread the listener runs on). Implementations
/// must be cheap and non-blocking on that path.
struct StreamRxAudioSink
{
    /// @param stream_index  STREAM_INPUT index the packet arrived on (0=AM824, 1=AAF).
    /// @param fmt           wire format of @p payload.
    /// @param payload       the AVTP audio payload (header already stripped).
    virtual void on_listener_audio(uint16_t stream_index, StreamAudioFormat fmt, std::span<uint8_t const> payload) = 0;

    StreamRxAudioSink() = default;
    StreamRxAudioSink(StreamRxAudioSink const&) = default;
    StreamRxAudioSink(StreamRxAudioSink&&) = default;
    auto operator=(StreamRxAudioSink const&) -> StreamRxAudioSink& = default;
    auto operator=(StreamRxAudioSink&&) -> StreamRxAudioSink& = default;
    virtual ~StreamRxAudioSink() = default;
};

}  // namespace statusbar::avb_entity
