#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// AudioEgress — the egress-side playout buffer for the inter-site audio tunnel.
///
/// Receives the AAF-v1/AnnexJ packets emitted by the far site's AudioIngest
/// (each carrying a TAI presentation time + interleaved PCM) and reclocks them
/// into the local media clock for transmission as a local AVB stream.
///
/// Both sites discipline CLOCK_REALTIME to GPS, so TAI is a single absolute
/// timeline shared by both ends: a packet's `src_tai` and the receiver's local
/// `now_tai` are directly comparable. The receiver presents each frame at
/// `src_tai + worst_case_latency (WCL)`, i.e. at local time `now_tai` it emits
/// the frame whose source presentation time is `now_tai − WCL`. The WCL window
/// absorbs tunnel transit + jitter; anything not yet arrived is concealed with
/// silence (drop-to-0).
///
/// Frame-index keying. The packet period (e.g. 44 frames = 458 us @ 96 kHz) is
/// not an integer number of ns, so quantizing TAI directly would drift. Instead
/// every TAI is converted to an exact absolute frame index
/// (`round((tai − epoch)·rate/1e9)`, 128-bit) and the PresentationSlotMap is
/// keyed by that frame index. Sub-packet serving (the media timer reads e.g. 12
/// frames at a time from 44-frame packets) then falls out for free.
///
/// Single-threaded: the owning thread (the media-timer thread) calls both
/// `submit` (draining packets handed over from the RX thread via an SPSC queue)
/// and `playout`. Pure: no I/O, no clocks, no heap. `MaxPacketBytes` bounds the
/// per-slot PCM buffer (frames_per_packet · channels · bytes_per_sample must fit).

#include "statusbar/container/container_presentation_slot_map.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>

namespace statusbar::udptun {

template <size_t MaxPacketBytes = 1536, size_t SlotCapacity = 64>
class AudioEgress
{
  public:
    struct Config
    {
        uint16_t channels{8};
        uint32_t sample_rate_hz{96'000};
        uint16_t bytes_per_sample{4};
        uint16_t frames_per_packet{44};
        int64_t wcl_ns{20'000'000};  // playout delay; MUST be <= ring depth (SlotCapacity*frames_per_packet/Fs)
    };

    explicit AudioEgress(Config cfg) noexcept
        : cfg_{cfg}
        , frame_bytes_{static_cast<size_t>(cfg.channels) * cfg.bytes_per_sample}
        , slots_{static_cast<int64_t>(cfg.frames_per_packet)}
    {}

    [[nodiscard]] auto frame_bytes() const noexcept -> size_t { return frame_bytes_; }
    [[nodiscard]] auto anchored() const noexcept -> bool { return anchored_; }

    /// Store a received packet (TAI of its first frame + interleaved PCM). The
    /// first accepted packet anchors the TAI→frame epoch. Returns false if the
    /// packet does not fit MaxPacketBytes or maps before the epoch.
    auto submit(int64_t const src_tai_ns, std::span<uint8_t const> const pcm, uint16_t const n_frames) -> bool
    {
        if (!anchored_) {
            epoch_tai_ns_ = src_tai_ns;
            anchored_ = true;
        }
        size_t const need = static_cast<size_t>(n_frames) * frame_bytes_;
        if (need == 0 || need > MaxPacketBytes || pcm.size() < need) {
            return false;
        }
        int64_t const fpp = static_cast<int64_t>(cfg_.frames_per_packet);
        int64_t const f = frame_for_tai(src_tai_ns);
        // The sender emits on frame-per-packet boundaries; round to the nearest
        // multiple (sign-correct: integer division truncates toward zero, which
        // would round a negative frame the wrong way).
        int64_t const frame_start = (f >= 0) ? (((f + (fpp / 2)) / fpp) * fpp) : -((((-f) + (fpp / 2)) / fpp) * fpp);
        if (frame_start < 0) {
            return false;
        }
        Packet p{};
        p.frame_start = frame_start;
        p.n_frames = n_frames;
        p.byte_len = static_cast<uint16_t>(need);
        std::memcpy(p.bytes.data(), pcm.data(), need);
        slots_.store(frame_start, p);
        return true;
    }

    /// Produce `n_frames` of interleaved PCM into `out` for local time `now_tai_ns`.
    /// Frames present in the buffer are copied; missing/late frames are zero-filled
    /// (drop-to-0 concealment). Returns the count of frames filled with real audio.
    auto playout(int64_t const now_tai_ns, std::span<uint8_t> out, uint16_t const n_frames) -> size_t
    {
        size_t const total = static_cast<size_t>(n_frames) * frame_bytes_;
        if (out.size() < total) {
            return 0;
        }
        std::memset(out.data(), 0, total);  // conceal by default
        if (!anchored_) {
            return 0;
        }
        int64_t const fpp = static_cast<int64_t>(cfg_.frames_per_packet);
        // CONTIGUOUS read cursor. The caller reads a GPS-paced VARIABLE length each
        // call (n_frames = nominal +/- 1) but on a FIXED-interval TAI grid. Anchoring
        // every read at frame_for_tai(now - wcl) then TEARS: it skips a frame when
        // n_frames < nominal and repeats one when n_frames > nominal -- a one-sample
        // slip on every off-nominal tick (inaudible on a slow/low tone, an audible
        // click on a fast one like 13 kHz). So advance the read position by the
        // frames ACTUALLY read, and only re-anchor to the TAI target when the cursor
        // has drifted far (tunnel re-establish, clock step, long-run drift) -- so a
        // re-sync costs at most one slip and is rare, instead of one slip per tick.
        int64_t const target = frame_for_tai(now_tai_ns - cfg_.wcl_ns);
        int64_t const resync_frames = (static_cast<int64_t>(SlotCapacity) * fpp) / 2;  // ~half the ring
        int64_t const drift = (read_cursor_ > target) ? (read_cursor_ - target) : (target - read_cursor_);
        // Continue contiguously while the cursor is locked and close to TAI; else
        // (un-locked startup, mid-stream underrun, or large drift) anchor to the TAI
        // target. The cursor is only locked once real audio is actually read (below),
        // so the pre-data startup silence never pins it to an empty region.
        int64_t const base = (cursor_valid_ && drift <= resync_frames) ? read_cursor_ : target;
        size_t real = 0;
        std::optional<Packet> cur{};
        int64_t cur_fs = std::numeric_limits<int64_t>::min();
        for (uint16_t i = 0; i < n_frames; ++i) {
            int64_t const f = base + static_cast<int64_t>(i);
            if (f < 0) {
                continue;
            }
            int64_t const fs = (f / fpp) * fpp;
            if (fs != cur_fs) {
                cur = slots_.load(f);
                cur_fs = fs;
            }
            if (cur.has_value()) {
                int64_t const off = f - cur->frame_start;
                if (off >= 0 && off < static_cast<int64_t>(cur->n_frames)) {
                    std::memcpy(
                        out.data() + (static_cast<size_t>(i) * frame_bytes_),
                        cur->bytes.data() + (static_cast<size_t>(off) * frame_bytes_),
                        frame_bytes_);
                    ++real;
                }
            }
        }
        // Lock + advance the cursor only after emitting real audio, so the pre-data
        // startup window (and any mid-stream underrun, real==0) re-anchors to live
        // data next time instead of pinning the cursor to an empty region.
        if (real > 0) {
            read_cursor_ = base + static_cast<int64_t>(n_frames);
            cursor_valid_ = true;
        } else {
            cursor_valid_ = false;
        }
        return real;
    }

    /// Drop all buffered packets and the TAI anchor so the next submit() re-anchors
    /// a fresh epoch on the next packet. Use when the inter-site tunnel (re)opens or
    /// a new stream begins, so a stale timeline can never leave the egress stuck
    /// emitting silence. After reset(), playout() returns 0 (silence) until the next
    /// submit() re-anchors.
    void reset() noexcept
    {
        slots_.reset();
        epoch_tai_ns_ = 0;
        anchored_ = false;
        cursor_valid_ = false;
    }

    /// Exact absolute frame index for a TAI time (round to nearest), via 128-bit
    /// math so it never overflows over a long session.
    [[nodiscard]] auto frame_for_tai(int64_t const tai_ns) const noexcept -> int64_t
    {
        __int128 num = static_cast<__int128>(tai_ns - epoch_tai_ns_) * static_cast<__int128>(cfg_.sample_rate_hz);
        __int128 const half = 500'000'000;  // 0.5 * 1e9 for round-to-nearest
        num += (num >= 0) ? half : -half;
        return static_cast<int64_t>(num / 1'000'000'000);
    }

  private:
    struct Packet
    {
        int64_t frame_start{0};
        uint16_t n_frames{0};
        uint16_t byte_len{0};
        std::array<uint8_t, MaxPacketBytes> bytes{};
    };

    Config cfg_;
    size_t frame_bytes_;
    container::PresentationSlotMap<Packet, SlotCapacity> slots_;
    int64_t epoch_tai_ns_{0};
    bool anchored_{false};
    // Contiguous playout cursor (see playout()): the absolute frame index of the
    // next read. Advances by the frames actually read so a variable read length
    // never tears against the fixed TAI grid; re-anchored to TAI only on large drift.
    int64_t read_cursor_{0};
    bool cursor_valid_{false};
};

}  // namespace statusbar::udptun
