#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// AAF reframer — turn a variable per-wake sample count into constant-size AAF
/// packets.
///
/// The media timer wakes on gPTP, but the media clock is GPS/TAI-paced, so the
/// two timebases drift and each wake yields a VARIABLE number of samples
/// (nominal ± 1). AM824 (IEC 61883-6) carries SYT/DBC and tolerates that
/// directly. AAF does NOT: `samples_per_frame` is fixed by the advertised stream
/// format, and a Milan listener rejects any packet whose payload doesn't decode
/// to exactly that count (it drops media lock and glitches).
///
/// This FIFO buffers the GPS-paced interleaved-float samples and emits only
/// whole `block_frames`-sized blocks, carrying the (< block) remainder to the
/// next wake. A wake therefore produces 0, 1, or 2+ AAF packets. Each emitted
/// block is tagged with the media-clock sample index of its first frame, so the
/// caller can stamp a jitter-free, evenly-spaced avtp_timestamp.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <vector>

namespace statusbar::avb_entity {

/// Fixed-capacity, allocation-free (after construction) interleaved-float FIFO
/// that re-blocks a variable input stream into constant `block_frames` blocks.
class AafReframer
{
  public:
    /// @param channels      interleaved channel count
    /// @param block_frames  output block size in frames-per-channel (e.g. 12)
    /// @param capacity_blocks  buffer headroom in whole blocks (>= 2 recommended)
    /// @param mr            memory resource for the backing buffer
    AafReframer(
        size_t channels,
        size_t block_frames,
        size_t capacity_blocks = 4,
        std::pmr::memory_resource* mr = std::pmr::get_default_resource())
        : channels_{channels}
        , block_frames_{block_frames}
        , cap_frames_{block_frames * std::max<size_t>(capacity_blocks, 2)}
        , buf_(cap_frames_ * channels, 0.0F, mr)
    {}

    /// Append `frames` interleaved frames from `src`, tagging the buffer's first
    /// frame with `first_index` (the media-clock sample index) when the FIFO is
    /// empty. If the backlog would overflow, the oldest frames are dropped (a
    /// safety net; steady state never overflows).
    void push(std::span<float const> src, uint16_t frames, uint64_t first_index) noexcept
    {
        if (frames == 0 || channels_ == 0) {
            return;
        }
        if (frames_ + frames > cap_frames_) {
            size_t const drop = std::min((frames_ + frames) - cap_frames_, frames_);
            pop_front(drop);
        }
        if (frames_ == 0) {
            first_index_ = first_index;
        }
        size_t const n = std::min<size_t>(frames, cap_frames_ - frames_);
        std::copy_n(src.begin(), n * channels_, buf_.begin() + static_cast<std::ptrdiff_t>(frames_ * channels_));
        frames_ += n;
    }

    /// Emit every whole block currently buffered. `emit(first_index, block)` is
    /// called once per block with the media-clock index of the block's first
    /// frame and a span of exactly `block_frames * channels` interleaved floats.
    template <typename Emit>
    void drain(Emit const& emit)
    {
        while (frames_ >= block_frames_) {
            // pop_front() advances first_index_ by block_frames_, so the next
            // iteration's first_index_ already points at the new front frame.
            emit(first_index_, std::span<float const>{buf_.data(), block_frames_ * channels_});
            pop_front(block_frames_);
        }
    }

    /// Drop any partial block (e.g. when the talker gate closes) so a later
    /// reconnect starts clean.
    void clear() noexcept { frames_ = 0; }

    [[nodiscard]] auto buffered_frames() const noexcept -> size_t { return frames_; }
    [[nodiscard]] auto block_frames() const noexcept -> size_t { return block_frames_; }

  private:
    void pop_front(size_t frames) noexcept
    {
        frames = std::min(frames, frames_);
        std::copy(
            buf_.begin() + static_cast<std::ptrdiff_t>(frames * channels_),
            buf_.begin() + static_cast<std::ptrdiff_t>(frames_ * channels_),
            buf_.begin());
        frames_ -= frames;
        first_index_ += frames;
    }

    size_t channels_;
    size_t block_frames_;
    size_t cap_frames_;
    std::pmr::vector<float> buf_;
    size_t frames_{0};         ///< complete frames buffered (per channel)
    uint64_t first_index_{0};  ///< media-clock index of buffered frame 0
};

}  // namespace statusbar::avb_entity
