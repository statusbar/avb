// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Unit tests for AafReframer — proving a variable per-wake sample count is
/// re-blocked into constant-size AAF packets with no samples lost or reordered.

#include "statusbar/avb_entity/avb_entity_aaf_reframe.hpp"

#include "statusbar/ptpclient/ptpclient_media_clock.hpp"
#include "statusbar/test/test.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using statusbar::avb_entity::AafReframer;

namespace {

// Build `frames` interleaved frames whose every sample is unique and encodes its
// absolute position: sample for (frame f, channel c) = (start_frame + f)*ch + c.
// This lets a test verify both ordering and that nothing is dropped/duplicated.
auto ramp(uint64_t start_frame, uint16_t frames, size_t channels) -> std::vector<float>
{
    std::vector<float> v(static_cast<size_t>(frames) * channels);
    for (uint16_t f = 0; f < frames; ++f) {
        for (size_t c = 0; c < channels; ++c) {
            v[(static_cast<size_t>(f) * channels) + c] = static_cast<float>(((start_frame + f) * channels) + c);
        }
    }
    return v;
}

struct EmittedBlock
{
    uint64_t first_index;
    std::vector<float> samples;
};

}  // namespace

TEST(aaf_reframe, variable_in_constant_out_no_loss)
{
    constexpr size_t kChannels = 2;
    constexpr size_t kBlock = 12;
    AafReframer rf{kChannels, kBlock};

    std::vector<EmittedBlock> blocks;
    auto collect = [&](uint64_t first_index, std::span<float const> block) {
        blocks.push_back({first_index, std::vector<float>(block.begin(), block.end())});
    };

    // Feed a realistic GPS-paced pattern: 12,13,11,12,12,13,11,...
    uint16_t const pattern[] = {12, 13, 11, 12, 12, 13, 11, 12, 13, 11};
    uint64_t idx = 0;
    uint64_t total_frames = 0;
    for (uint16_t n : pattern) {
        auto buf = ramp(idx, n, kChannels);
        rf.push(buf, n, idx);
        rf.drain(collect);
        idx += n;
        total_frames += n;
    }

    // Every emitted block is exactly kBlock frames.
    for (auto const& b : blocks) {
        EXPECT_EQ(b.samples.size(), kBlock * kChannels);
    }
    // Blocks emitted = floor(total / kBlock); remainder still buffered.
    EXPECT_EQ(blocks.size(), static_cast<size_t>(total_frames / kBlock));
    EXPECT_EQ(rf.buffered_frames(), static_cast<size_t>(total_frames % kBlock));

    // Concatenated output is the exact contiguous ramp 0,1,2,... (no loss/dupes).
    uint64_t expected = 0;
    for (auto const& b : blocks) {
        for (float s : b.samples) {
            EXPECT_EQ(s, static_cast<float>(expected));
            ++expected;
        }
    }
    // first_index of each block steps by exactly kBlock from 0.
    for (size_t i = 0; i < blocks.size(); ++i) {
        EXPECT_EQ(blocks[i].first_index, static_cast<uint64_t>(i * kBlock));
    }
}

TEST(aaf_reframe, two_blocks_in_one_drain)
{
    constexpr size_t kChannels = 4;
    constexpr size_t kBlock = 12;
    AafReframer rf{kChannels, kBlock};
    size_t count = 0;
    auto buf = ramp(0, 25, kChannels);  // 25 frames -> 2 full blocks, 1 left
    rf.push(buf, 25, 0);
    rf.drain([&](uint64_t, std::span<float const>) { ++count; });
    EXPECT_EQ(count, 2U);
    EXPECT_EQ(rf.buffered_frames(), 1U);
}

TEST(aaf_reframe, clear_drops_partial)
{
    AafReframer rf{8, 12};
    auto buf = ramp(0, 5, 8);
    rf.push(buf, 5, 100);
    EXPECT_EQ(rf.buffered_frames(), 5U);
    rf.clear();
    EXPECT_EQ(rf.buffered_frames(), 0U);
    // After clear, the next push re-tags first_index from the new data.
    bool emitted = false;
    uint64_t got_index = 0;
    auto buf2 = ramp(0, 12, 8);
    rf.push(buf2, 12, 500);
    rf.drain([&](uint64_t first_index, std::span<float const>) {
        emitted = true;
        got_index = first_index;
    });
    EXPECT_TRUE(emitted);
    EXPECT_EQ(got_index, 500U);
}

TEST(aaf_reframe, first_index_tracks_carry)
{
    constexpr size_t kChannels = 2;
    AafReframer rf{kChannels, 12};
    std::vector<uint64_t> indices;
    auto collect = [&](uint64_t first_index, std::span<float const>) { indices.push_back(first_index); };

    auto a = ramp(0, 13, kChannels);  // emit 1 block @0, carry 1 frame (index 12)
    rf.push(a, 13, 0);
    rf.drain(collect);
    auto b = ramp(13, 13, kChannels);  // now 14 buffered -> emit 1 block @12, carry 2
    rf.push(b, 13, 13);
    rf.drain(collect);

    EXPECT_EQ(indices.size(), 2U);
    EXPECT_EQ(indices[0], 0U);
    EXPECT_EQ(indices[1], 12U);
    EXPECT_EQ(rf.buffered_frames(), 2U);
}

namespace {

struct DriveResult
{
    int64_t max_delta_err{0};   ///< worst |consecutive avtp_ts delta - period*r|, ns
    int64_t expected_delta{0};  ///< llround(period_ns * r)
    size_t packets{0};
    bool saw_zero{false};  ///< a wake that emitted 0 packets
    bool saw_two{false};   ///< a wake that emitted >= 2 packets
};

// Drive the REAL media clock + reframer exactly as the entity does: a gPTP-paced
// 125 us wake, a GPS/TAI-rate media clock at ratio r (so the per-wake sample
// count drifts off nominal), each emitted AAF block stamped with
// media_clock.timestamp_for(first_index) -- and measure the per-packet timestamp
// step. r>1 emits fewer samples/wake (0/1 packets); r<1 emits more (1/2 packets).
auto drive(double r) -> DriveResult
{
    using statusbar::ptpclient::MediaClockGenerator;
    constexpr size_t kChannels = 2;
    constexpr uint32_t kBlock = 12;
    constexpr double kSampleRate = 96000.0;
    double const period_ns = static_cast<double>(kBlock) * (1e9 / kSampleRate);  // 125000

    MediaClockGenerator mc{MediaClockGenerator::Config{.sample_rate_hz = kSampleRate, .presentation_offset_ns = 1'000'000}};
    AafReframer rf{kChannels, kBlock};

    DriveResult out{};
    out.expected_delta = static_cast<int64_t>(std::llround(period_ns * r));
    std::vector<uint64_t> ts;
    uint64_t wake = 1'000'000'000ULL;
    for (int i = 0; i < 400; ++i) {
        auto const tick = mc.advance(wake, r, kBlock);
        std::vector<float> buf(static_cast<size_t>(tick.samples) * kChannels, 0.0F);
        rf.push(buf, static_cast<uint16_t>(tick.samples), tick.first_index);
        size_t const before = ts.size();
        rf.drain([&](uint64_t first_index, std::span<float const>) { ts.push_back(mc.timestamp_for(first_index)); });
        size_t const n = ts.size() - before;
        out.saw_zero = out.saw_zero || (n == 0);
        out.saw_two = out.saw_two || (n >= 2);
        wake += static_cast<uint64_t>(period_ns);
    }
    out.packets = ts.size();
    for (size_t i = 1; i < ts.size(); ++i) {
        int64_t const delta = static_cast<int64_t>(ts[i]) - static_cast<int64_t>(ts[i - 1]);
        int64_t const err = delta - out.expected_delta;
        out.max_delta_err = std::max(out.max_delta_err, err < 0 ? -err : err);
    }
    return out;
}

}  // namespace

// The avtp_timestamp must advance by EXACTLY one packet period * r per AAF packet
// (TAI synthesis), regardless of whether a wake emitted 0, 1, or 2 packets.
TEST(aaf_reframe, avtp_timestamp_steps_by_period_times_r_when_r_gt_1)
{
    auto const res = drive(1.0 + 50e-6);  // +50 ppm -> some wakes emit 0 packets
    EXPECT_TRUE(res.packets > 250U);
    EXPECT_TRUE(res.saw_zero);
    EXPECT_TRUE(res.max_delta_err <= 1);  // 125000*r per packet, within 1 ns rounding
}

TEST(aaf_reframe, avtp_timestamp_steps_by_period_times_r_when_r_lt_1)
{
    // r=0.985 makes the media clock run ~1.5% fast vs the gPTP wake, so it
    // regularly produces 13 samples/wake and the reframer emits 2 packets in some
    // wakes -- exercising the constant step under a same-wake double emit.
    auto const res = drive(0.985);
    EXPECT_TRUE(res.packets > 250U);
    EXPECT_TRUE(res.saw_two);
    EXPECT_TRUE(res.max_delta_err <= 1);  // step stays clean even with 2 packets/wake
}

TEST_MAIN(statusbar_avb_entity, avb_entity_aaf_reframe_test)
