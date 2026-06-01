#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// AudioReframer — pure ingest reframer for the inter-site audio tunnel.
///
/// Local AVB audio arrives at the AVB cadence (e.g. 12 interleaved PCM frames
/// every 125 µs for a Class A 96 kHz stream). The inter-site UDPTUN packets are
/// coarser (the deployment uses 1 ms = 96 frames per packet, ~1000 pkt/s, to
/// keep the WAN packet rate low). The reframer accumulates incoming frames into
/// fixed-size packets and stamps each completed packet with the **TAI**
/// presentation time of its first frame, ready for AafV1OverAnnexJCodec to
/// encode. It is pure (no I/O, no clocks, no heap) and single-threaded.
///
/// The caller supplies, per push, the TAI presentation time of the first frame
/// in that push; the reframer derives each subsequent frame's time by adding
/// `ns_per_frame`. A push may complete zero, one, or several packets (and leave
/// a partial packet buffered for the next push). Discontinuities (a gap in the
/// source stream) are the caller's responsibility to handle before pushing —
/// the reframer assumes the frames it is given are contiguous; call `reset()`
/// at a discontinuity to drop the partial packet and re-anchor.
///
/// `MaxPacketBytes` bounds the embedded buffer: frames_per_packet * channels *
/// bytes_per_sample must not exceed it (checked at construction).

#include "statusbar/status/statusbar_assert.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace statusbar::udptun {

template <size_t MaxPacketBytes = 4096>
class AudioReframer
{
  public:
    struct Config
    {
        uint16_t channels{8};
        uint16_t frames_per_packet{96};  // 1 ms @ 96 kHz
        uint16_t bytes_per_sample{4};    // int32 / float32
        int64_t ns_per_frame{10'417};    // 1e9 / 96000, rounded; caller sets exact
    };

    /// A completed packet: TAI presentation time of its first frame plus the
    /// interleaved PCM bytes. `pcm` views the reframer's internal buffer and is
    /// valid only until the next `push`/`reset` call — copy or consume it in the
    /// emit callback.
    struct Packet
    {
        int64_t tai_ns{0};
        std::span<uint8_t const> pcm{};
    };

    explicit AudioReframer(Config cfg) noexcept
        : cfg_{cfg}
        , frame_bytes_{static_cast<size_t>(cfg.channels) * cfg.bytes_per_sample}
        , packet_bytes_{static_cast<size_t>(cfg.frames_per_packet) * frame_bytes_}
    {
        STATUSBAR_ASSERT(cfg_.channels > 0 && cfg_.frames_per_packet > 0 && cfg_.bytes_per_sample > 0);
        STATUSBAR_ASSERT(packet_bytes_ <= MaxPacketBytes && "AudioReframer: packet exceeds MaxPacketBytes");
    }

    [[nodiscard]] auto frame_bytes() const noexcept -> size_t { return frame_bytes_; }
    [[nodiscard]] auto packet_bytes() const noexcept -> size_t { return packet_bytes_; }
    [[nodiscard]] auto buffered_frames() const noexcept -> uint16_t { return fill_frames_; }

    /// Drop any partial packet and re-anchor. Call at a stream discontinuity.
    void reset() noexcept { fill_frames_ = 0; }

    /// Push `n_frames` interleaved PCM frames whose first frame is presented at
    /// `first_frame_tai_ns`. Invokes `emit(Packet const&)` once per completed
    /// packet. Returns the number of packets emitted.
    template <typename Emit>
    auto push(int64_t const first_frame_tai_ns, std::span<uint8_t const> const interleaved, uint16_t const n_frames, Emit emit)
        -> size_t
    {
        size_t const have_bytes = interleaved.size();
        size_t const need_bytes = static_cast<size_t>(n_frames) * frame_bytes_;
        STATUSBAR_ASSERT(have_bytes >= need_bytes);
        size_t emitted = 0;
        for (uint16_t i = 0; i < n_frames; ++i) {
            if (fill_frames_ == 0) {
                packet_start_tai_ = first_frame_tai_ns + (static_cast<int64_t>(i) * cfg_.ns_per_frame);
            }
            std::memcpy(
                buf_.data() + (static_cast<size_t>(fill_frames_) * frame_bytes_),
                interleaved.data() + (static_cast<size_t>(i) * frame_bytes_),
                frame_bytes_);
            ++fill_frames_;
            if (fill_frames_ == cfg_.frames_per_packet) {
                emit(Packet{.tai_ns = packet_start_tai_, .pcm = std::span<uint8_t const>{buf_.data(), packet_bytes_}});
                ++emitted;
                fill_frames_ = 0;
            }
        }
        return emitted;
    }

  private:
    Config cfg_;
    size_t frame_bytes_;
    size_t packet_bytes_;
    std::array<uint8_t, MaxPacketBytes> buf_{};
    uint16_t fill_frames_{0};
    int64_t packet_start_tai_{0};
};

}  // namespace statusbar::udptun
