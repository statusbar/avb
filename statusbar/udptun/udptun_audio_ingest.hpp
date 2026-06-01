#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// AudioIngest — the ingest-side logic for the inter-site audio tunnel.
///
/// Composes AudioReframer with an exact TAI stamper. Local AVB audio is captured
/// by a GPS-rate-locked listener, so its frame *rate* is exact; the only thing
/// missing is an absolute, leap-immune timeline both sites agree on. We get that
/// by reading CLOCK_TAI ONCE at stream start (`start(anchor_tai_ns)`) and then
/// advancing the running TAI by the exact duration of each batch of frames —
/// `n_frames * 1e9 / sample_rate` — using a remainder accumulator so there is no
/// rounding drift (12 frames @ 96 kHz = exactly 125000 ns). Re-reading CLOCK_TAI
/// per packet would instead inject the clock-read jitter into every timestamp;
/// anchoring once and counting frames keeps the emitted presentation times
/// perfectly smooth and locked to the audio it carries.
///
/// Each batch of frames is pushed through the reframer; when a full inter-site
/// packet (default 1 ms / 96 frames) completes, `submit` invokes the sink with
/// the packet's TAI presentation time and its PCM bytes — ready for
/// AafV1OverAnnexJCodec to encode and the udptun session to send. The far end
/// presents at `tai + worst_case_latency`. Pure (no I/O, no clocks, no heap);
/// the caller supplies the CLOCK_TAI anchor and owns the socket.
///
/// Single-threaded. Call `start()` again (with a fresh anchor) on a stream
/// restart / media-clock reset to re-anchor and drop any partial packet.

#include "statusbar/status/statusbar_assert.hpp"
#include "statusbar/udptun/udptun_audio_reframe.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace statusbar::udptun {

template <size_t MaxPacketBytes = 4096>
class AudioIngest
{
  public:
    struct Config
    {
        uint16_t channels{8};
        uint32_t sample_rate_hz{96'000};
        uint16_t bytes_per_sample{4};           // int32 / float32
        uint16_t tunnel_frames_per_packet{96};  // 1 ms @ 96 kHz
    };

    using Packet = typename AudioReframer<MaxPacketBytes>::Packet;

    explicit AudioIngest(Config cfg) noexcept
        : cfg_{cfg}
        , reframer_{make_reframer_config(cfg)}
    {
        STATUSBAR_ASSERT(cfg_.sample_rate_hz > 0);
    }

    /// Anchor the TAI timeline (read CLOCK_TAI at stream start) and drop any
    /// partial packet. Until called, `submit` is a no-op.
    void start(int64_t const anchor_tai_ns) noexcept
    {
        running_tai_ns_ = anchor_tai_ns;
        acc_num_ = 0;
        started_ = true;
        reframer_.reset();
    }

    [[nodiscard]] auto started() const noexcept -> bool { return started_; }
    [[nodiscard]] auto running_tai_ns() const noexcept -> int64_t { return running_tai_ns_; }

    /// Steer the free-running presentation TAI toward an externally supplied live
    /// GPS-TAI target (from the gPTP->GPS-TAI translator). The header design assumes
    /// the source's frame rate is exactly `sample_rate_hz` in GPS-TAI; that holds
    /// only when the source media clock is GPS-locked. A source slaved to the local
    /// gPTP (e.g. a the audio interface loopback following the switch GM, not a GPS CRF) runs tens
    /// to hundreds of ppm off GPS, so the pure frame-counted `running_tai` slides
    /// out of the far egress window (observed: +172 ppm -> +500 ms over an hour).
    /// `discipline` applies a first-order slew — a small fraction of the (target -
    /// running) error per call, clamped — so the long-term rate locks to GPS-TAI
    /// while the emitted presentation times stay smooth and monotonic (steady-state
    /// adjustment is sub-microsecond per call). Call once per submitted batch with
    /// the live GPS-TAI for "now". No-op before `start()`. Keeping `start()`'s
    /// anchor near the live TAI keeps the error tiny, so the clamp only guards
    /// against pathological translator transients.
    void discipline(int64_t const target_tai_ns) noexcept
    {
        if (!started_) {
            return;
        }
        int64_t const err = target_tai_ns - running_tai_ns_;
        int64_t step = err / SLEW_DIVISOR;  // proportional, exponential convergence
        if (step > MAX_STEP_NS) {
            step = MAX_STEP_NS;
        } else if (step < -MAX_STEP_NS) {
            step = -MAX_STEP_NS;
        } else if (step == 0 && err != 0) {
            step = (err > 0) ? 1 : -1;  // always creep toward a sub-divisor error
        }
        running_tai_ns_ += step;
    }

    /// Push `n_frames` interleaved PCM frames captured contiguously from the
    /// local stream. The batch's first frame is stamped with the current running
    /// TAI; the running TAI then advances by the exact duration of the batch.
    /// Invokes `sink(Packet const&)` for each completed inter-site packet.
    /// Returns the number of packets emitted (0 before `start()`).
    template <typename Sink>
    auto submit(std::span<uint8_t const> const interleaved, uint16_t const n_frames, Sink sink) -> size_t
    {
        if (!started_) {
            return 0;
        }
        size_t const emitted = reframer_.push(running_tai_ns_, interleaved, n_frames, sink);
        advance_tai(n_frames);
        return emitted;
    }

  private:
    // discipline() slew: correct 1/SLEW_DIVISOR of the GPS-TAI error per call
    // (exponential convergence; steady-state residual ~= drift_per_call *
    // SLEW_DIVISOR, i.e. ~1.4 us at 172 ppm / 8 kHz), clamped to MAX_STEP_NS so a
    // pathological target never injects an audible jump into the presentation time.
    static constexpr int64_t SLEW_DIVISOR = 64;
    static constexpr int64_t MAX_STEP_NS = 50'000;  // 50 us safety clamp per call

    static auto make_reframer_config(Config const& c) noexcept -> typename AudioReframer<MaxPacketBytes>::Config
    {
        typename AudioReframer<MaxPacketBytes>::Config rc{};
        rc.channels = c.channels;
        rc.frames_per_packet = c.tunnel_frames_per_packet;
        rc.bytes_per_sample = c.bytes_per_sample;
        // Intra-batch per-frame stamp (only matters if a packet boundary falls
        // mid-batch); the exact running TAI corrects at every batch boundary.
        rc.ns_per_frame = static_cast<int64_t>(1'000'000'000LL / c.sample_rate_hz);
        return rc;
    }

    // Advance running_tai_ns_ by exactly n_frames * 1e9 / sample_rate using a
    // remainder accumulator (Bresenham) so there is no long-term rounding drift.
    void advance_tai(uint16_t const n_frames) noexcept
    {
        acc_num_ += static_cast<int64_t>(n_frames) * 1'000'000'000LL;
        running_tai_ns_ += acc_num_ / static_cast<int64_t>(cfg_.sample_rate_hz);
        acc_num_ %= static_cast<int64_t>(cfg_.sample_rate_hz);
    }

    Config cfg_;
    AudioReframer<MaxPacketBytes> reframer_;
    int64_t running_tai_ns_{0};
    int64_t acc_num_{0};
    bool started_{false};
};

}  // namespace statusbar::udptun
