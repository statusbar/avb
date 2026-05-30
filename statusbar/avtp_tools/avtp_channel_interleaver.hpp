#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Per-channel-callback → interleaved-sample buffer adapter. AVTP audio
// stream-input deserializers fire a per-channel callback once each
// packet (one call per channel with that channel's samples). Most sinks
// — Bw64 writer, ALSA, Core Audio output — want interleaved samples.
// ChannelInterleaver bridges the two by routing each per-channel callback
// into the correct lane of a caller-owned scratch buffer; after all
// channels have deposited, the buffer is ready to ship.
//
// Caller owns the scratch vector; the interleaver only references it. This
// matches the use case where the caller reuses a single scratch across
// many packets (avoiding per-packet allocations).

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace statusbar::avtp_tools {

class ChannelInterleaver
{
  public:
    ChannelInterleaver(std::vector<float>& scratch, uint16_t channel_count) noexcept
        : scratch_{scratch}
        , channel_count_{channel_count}
    {}

    // Resize the scratch to (samples_per_frame * channel_count) and zero
    // it. Must be called once per packet before the per-channel deposits;
    // the zero-fill provides silence for any channel the caller doesn't
    // deposit into.
    void prepare(size_t samples_per_frame)
    {
        samples_per_frame_ = samples_per_frame;
        scratch_.assign(samples_per_frame * channel_count_, 0.0f);
    }

    // Route one channel's samples into the interleaved scratch.
    // Channels >= channel_count are silently dropped.
    void deposit(uint8_t channel, std::span<float const> samples) noexcept
    {
        if (channel >= channel_count_) {
            return;
        }
        size_t const n = samples.size() < samples_per_frame_ ? samples.size() : samples_per_frame_;
        for (size_t i = 0; i < n; ++i) {
            scratch_[(i * channel_count_) + channel] = samples[i];
        }
    }

    [[nodiscard]] auto samples_written() const noexcept -> size_t { return samples_per_frame_ * channel_count_; }

  private:
    std::vector<float>& scratch_;
    uint16_t channel_count_;
    size_t samples_per_frame_{0};
};

}  // namespace statusbar::avtp_tools
