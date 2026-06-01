// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_audio_reframe.hpp"

#include "statusbar/test/test.hpp"

#include <cstdint>
#include <span>
#include <vector>

using namespace statusbar;

namespace {

// Small config: 2 channels, 4 frames/packet, 2 bytes/sample, 1000 ns/frame.
auto small_config() -> udptun::AudioReframer<256>::Config
{
    udptun::AudioReframer<256>::Config c{};
    c.channels = 2;
    c.frames_per_packet = 4;
    c.bytes_per_sample = 2;
    c.ns_per_frame = 1000;
    return c;
}

// Interleaved PCM where frame f, channel ch byte b = (f, ch*... ) recognizable:
// each byte = (start_frame + f) so packets are easy to verify.
auto make_frames(uint8_t start_frame, uint16_t n_frames, size_t frame_bytes) -> std::vector<uint8_t>
{
    std::vector<uint8_t> v(static_cast<size_t>(n_frames) * frame_bytes, 0);
    for (uint16_t f = 0; f < n_frames; ++f) {
        for (size_t b = 0; b < frame_bytes; ++b) {
            v[(f * frame_bytes) + b] = static_cast<uint8_t>(start_frame + f);
        }
    }
    return v;
}

}  // namespace

TEST(audio_reframe_sizes, reports_frame_and_packet_bytes)
{
    udptun::AudioReframer<256> rf{small_config()};
    EXPECT_EQ(rf.frame_bytes(), size_t{4});    // 2 ch * 2 bytes
    EXPECT_EQ(rf.packet_bytes(), size_t{16});  // 4 frames * 4 bytes
    EXPECT_EQ(rf.buffered_frames(), uint16_t{0});
}

// One push of exactly one packet's worth → one emit, correct TAI, correct bytes.
TEST(audio_reframe_exact, single_packet)
{
    udptun::AudioReframer<256> rf{small_config()};
    auto const frames = make_frames(/*start*/ 0, /*n*/ 4, rf.frame_bytes());

    std::vector<int64_t> tais;
    std::vector<std::vector<uint8_t>> packets;
    auto const n = rf.push(/*first_tai*/ 5'000, frames, 4, [&](auto const& pkt) {
        tais.push_back(pkt.tai_ns);
        packets.emplace_back(pkt.pcm.begin(), pkt.pcm.end());
    });

    EXPECT_EQ(n, size_t{1});
    EXPECT_EQ(tais.size(), size_t{1});
    EXPECT_EQ(tais[0], int64_t{5'000});  // first frame's TAI
    EXPECT_EQ(packets[0].size(), size_t{16});
    EXPECT_EQ(packets[0][0], uint8_t{0});   // frame 0
    EXPECT_EQ(packets[0][15], uint8_t{3});  // frame 3, last byte
    EXPECT_EQ(rf.buffered_frames(), uint16_t{0});
}

// Sub-packet pushes accumulate; packet completes on the push that fills it, and
// its TAI is the TAI of the FIRST frame (from the earlier push).
TEST(audio_reframe_accumulate, partial_then_complete)
{
    udptun::AudioReframer<256> rf{small_config()};

    std::vector<int64_t> tais;
    // Push 3 frames starting at TAI 10000 (ns_per_frame=1000): frames at 10000,11000,12000.
    auto const a = make_frames(0, 3, rf.frame_bytes());
    auto n1 = rf.push(10'000, a, 3, [&](auto const& p) { tais.push_back(p.tai_ns); });
    EXPECT_EQ(n1, size_t{0});
    EXPECT_EQ(rf.buffered_frames(), uint16_t{3});

    // Push 1 more frame (TAI 13000) — completes the 4-frame packet.
    auto const b = make_frames(3, 1, rf.frame_bytes());
    auto n2 = rf.push(13'000, b, 1, [&](auto const& p) { tais.push_back(p.tai_ns); });
    EXPECT_EQ(n2, size_t{1});
    EXPECT_EQ(tais.size(), size_t{1});
    EXPECT_EQ(tais[0], int64_t{10'000});  // first frame of the packet
    EXPECT_EQ(rf.buffered_frames(), uint16_t{0});
}

// A push larger than one packet splits into multiple packets with correct TAIs.
TEST(audio_reframe_split, multi_packet_push)
{
    udptun::AudioReframer<256> rf{small_config()};
    auto const frames = make_frames(0, 9, rf.frame_bytes());  // 2 full packets + 1 leftover

    std::vector<int64_t> tais;
    auto const n = rf.push(20'000, frames, 9, [&](auto const& p) { tais.push_back(p.tai_ns); });

    EXPECT_EQ(n, size_t{2});
    EXPECT_EQ(tais.size(), size_t{2});
    EXPECT_EQ(tais[0], int64_t{20'000});           // frame 0
    EXPECT_EQ(tais[1], int64_t{24'000});           // frame 4 (4 * 1000 ns later)
    EXPECT_EQ(rf.buffered_frames(), uint16_t{1});  // frame 8 left over
}

// reset() drops the partial packet so the next frame re-anchors the TAI.
TEST(audio_reframe_reset, drops_partial)
{
    udptun::AudioReframer<256> rf{small_config()};
    auto const a = make_frames(0, 2, rf.frame_bytes());
    rf.push(1'000, a, 2, [](auto const&) {});
    EXPECT_EQ(rf.buffered_frames(), uint16_t{2});

    rf.reset();
    EXPECT_EQ(rf.buffered_frames(), uint16_t{0});

    // After reset, a fresh 4-frame push emits one packet anchored at the new TAI.
    std::vector<int64_t> tais;
    auto const b = make_frames(0, 4, rf.frame_bytes());
    auto const n = rf.push(99'000, b, 4, [&](auto const& p) { tais.push_back(p.tai_ns); });
    EXPECT_EQ(n, size_t{1});
    EXPECT_EQ(tais[0], int64_t{99'000});
}

// Default config sanity: 8ch / 96 frames / int32 = 3072 bytes, fits 4096.
TEST(audio_reframe_default, one_ms_packet_dimensions)
{
    udptun::AudioReframer<> rf{udptun::AudioReframer<>::Config{}};
    EXPECT_EQ(rf.frame_bytes(), size_t{32});     // 8 ch * 4 bytes
    EXPECT_EQ(rf.packet_bytes(), size_t{3072});  // 96 frames * 32 bytes
}

TEST_MAIN(statusbar_udptun, udptun_audio_reframe_test)
