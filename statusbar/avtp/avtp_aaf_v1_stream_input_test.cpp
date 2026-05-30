// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_aaf_v1_stream_input.hpp"

#include "statusbar/test/test.hpp"

#include <cstdint>
#include <vector>

using namespace statusbar;
using namespace statusbar::avtp;
using namespace statusbar::tsn;

// ============================================================================
// AafV1StreamInputContext construction and reset
// ============================================================================

TEST(aaf_v1_stream_input_ctx, construction)
{
    AafV1StreamInputContext ctx{AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32};
    EXPECT_EQ(ctx.channel_count, 2);
    EXPECT_EQ(ctx.bit_depth, 32);
    EXPECT_EQ(ctx.sample_period_ns, 20833U);
    EXPECT_FALSE(ctx.has_valid_timestamp);
    EXPECT_EQ(ctx.packets_received, 0U);
    EXPECT_EQ(ctx.last_sequence_num, 0U);
}

TEST(aaf_v1_stream_input_ctx, reset)
{
    AafV1StreamInputContext ctx{AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32};
    ctx.packets_received = 50;
    ctx.has_valid_timestamp = true;
    ctx.last_pts_ns = 12345;
    ctx.grandmaster_changed = true;
    ctx.reset();
    EXPECT_EQ(ctx.packets_received, 0U);
    EXPECT_FALSE(ctx.has_valid_timestamp);
    EXPECT_FALSE(ctx.grandmaster_changed);
    EXPECT_EQ(ctx.last_pts_ns, 0U);
    EXPECT_EQ(ctx.channel_count, 2);
}

// ============================================================================
// 32-bit sequence gap detection
// ============================================================================

TEST(aaf_v1_stream_input_seq, no_gap)
{
    AafV1StreamInputContext ctx{AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32};

    AafV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32);

    pdu.set_sequence_num(0);
    ctx.process_packet_header(pdu, 1000);
    pdu.set_sequence_num(1);
    ctx.process_packet_header(pdu, 2000);
    pdu.set_sequence_num(2);
    ctx.process_packet_header(pdu, 3000);
    EXPECT_EQ(ctx.sequence_gaps, 0U);
}

TEST(aaf_v1_stream_input_seq, gap_detected)
{
    AafV1StreamInputContext ctx{AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32};

    AafV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32);

    pdu.set_sequence_num(0);
    ctx.process_packet_header(pdu, 1000);
    pdu.set_sequence_num(5);  // gap: expected 1
    ctx.process_packet_header(pdu, 2000);
    EXPECT_EQ(ctx.sequence_gaps, 1U);
}

TEST(aaf_v1_stream_input_seq, wrap_32bit_no_gap)
{
    AafV1StreamInputContext ctx{AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32};

    AafV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32);

    pdu.set_sequence_num(0xFFFFFFFF);
    ctx.process_packet_header(pdu, 1000);
    pdu.set_sequence_num(0);  // wrap — not a gap
    ctx.process_packet_header(pdu, 2000);
    EXPECT_EQ(ctx.sequence_gaps, 0U);
}

// ============================================================================
// 64-bit timestamp — no reconstruction needed
// ============================================================================

TEST(aaf_v1_stream_input_ts, direct_64bit_timestamp)
{
    AafV1StreamInputContext ctx{AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32};

    AafV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32);
    pdu.set_tv(true);

    // Set a timestamp larger than 2^32 — impossible with V0
    uint64_t const large_ts = 10'000'000'000ULL;  // 10 seconds in ns
    pdu.set_avtp_timestamp(large_ts);
    pdu.set_sequence_num(0);

    ctx.process_packet_header(pdu, large_ts - 2'000'000);  // received 2ms before PTS
    EXPECT_TRUE(ctx.has_valid_timestamp);
    EXPECT_EQ(ctx.last_pts_ns, large_ts);
    EXPECT_EQ(ctx.compute_pts_ns(), large_ts);
}

TEST(aaf_v1_stream_input_ts, pts_extrapolation)
{
    AafV1StreamInputContext ctx{AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32};

    AafV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32);
    pdu.set_tv(true);
    pdu.set_avtp_timestamp(1'000'000'000ULL);
    pdu.set_sequence_num(0);

    ctx.process_packet_header(pdu, 999'000'000);
    uint64_t const pts0 = ctx.compute_pts_ns();
    EXPECT_EQ(pts0, 1'000'000'000ULL);

    ctx.advance_samples(6);

    // Next packet without timestamp
    pdu.set_tv(false);
    pdu.set_sequence_num(1);
    ctx.process_packet_header(pdu, 999'125'000);
    uint64_t const pts1 = ctx.compute_pts_ns();
    // Should be anchor + 6 samples * sample_period
    EXPECT_EQ(pts1, 1'000'000'000ULL + 6 * ctx.sample_period_ns);
}

// ============================================================================
// Grandmaster tracking
// ============================================================================

TEST(aaf_v1_stream_input_gm, grandmaster_change_detected)
{
    AafV1StreamInputContext ctx{AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32};

    AafV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32);
    pdu.set_tv(true);
    pdu.set_avtp_timestamp(1000);
    pdu.set_sequence_num(0);

    ClockIdentity gm1(0x0011223344556677ULL);
    pdu.set_ptp_grandmaster_identity(gm1);
    ctx.process_packet_header(pdu, 1000);
    EXPECT_FALSE(ctx.grandmaster_changed);
    EXPECT_EQ(ctx.last_grandmaster, gm1);

    // Same grandmaster
    pdu.set_sequence_num(1);
    ctx.process_packet_header(pdu, 2000);
    EXPECT_FALSE(ctx.grandmaster_changed);

    // Different grandmaster
    ClockIdentity gm2(0xAABBCCDDEEFF0011ULL);
    pdu.set_ptp_grandmaster_identity(gm2);
    pdu.set_sequence_num(2);
    ctx.process_packet_header(pdu, 3000);
    EXPECT_TRUE(ctx.grandmaster_changed);
    EXPECT_EQ(ctx.last_grandmaster, gm2);
}

// ============================================================================
// Full deserialize round-trip
// ============================================================================

TEST(aaf_v1_stream_input_deser, float32_stereo)
{
    AafV1StreamInputContext ctx{AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32};

    AafV1Pdu pdu{};
    StreamId sid(ieee::Eui48(0x00, 0x11, 0x22, 0x33, 0x44, 0x55), 0x0001);
    pdu.init(sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32);
    pdu.set_tv(true);
    pdu.set_avtp_timestamp(5'000'000'000ULL);
    pdu.set_sequence_num(42);
    pdu.set_dimensions(2, 2);  // 2 samples, 2 channels

    // Build payload: 2 samples * 2 channels * 4 bytes = 16 bytes
    // Wire format: [ch0_s0, ch1_s0, ch0_s1, ch1_s1] in big-endian float
    std::array<uint8_t, 16> payload{};
    float const val_ch0_s0 = 0.5F;
    float const val_ch1_s0 = -0.5F;
    float const val_ch0_s1 = 0.25F;
    float const val_ch1_s1 = -0.25F;
    size_t off = 0;
    (void)encode_aaf_sample(val_ch0_s0, payload, off, AafFormat::float_32bit);
    off += 4;
    (void)encode_aaf_sample(val_ch1_s0, payload, off, AafFormat::float_32bit);
    off += 4;
    (void)encode_aaf_sample(val_ch0_s1, payload, off, AafFormat::float_32bit);
    off += 4;
    (void)encode_aaf_sample(val_ch1_s1, payload, off, AafFormat::float_32bit);

    std::vector<float> received_ch0;
    std::vector<float> received_ch1;
    uint64_t received_pts = 0;

    aaf_v1_stream_deserialize(
        ctx,
        pdu,
        std::span<uint8_t const>(payload),
        4'998'000'000ULL,
        [&](uint8_t ch, std::span<float> samples, uint64_t pts, uint64_t /*period*/) {
            received_pts = pts;
            if (ch == 0) {
                received_ch0.assign(samples.begin(), samples.end());
            } else {
                received_ch1.assign(samples.begin(), samples.end());
            }
        });

    EXPECT_EQ(ctx.packets_received, 1U);
    EXPECT_EQ(received_pts, 5'000'000'000ULL);
    EXPECT_EQ(received_ch0.size(), 2U);
    EXPECT_EQ(received_ch1.size(), 2U);
    EXPECT_EQ(received_ch0[0], val_ch0_s0);
    EXPECT_EQ(received_ch0[1], val_ch0_s1);
    EXPECT_EQ(received_ch1[0], val_ch1_s0);
    EXPECT_EQ(received_ch1[1], val_ch1_s1);
}

TEST_MAIN(statusbar_avtp, avtp_aaf_v1_stream_input_test)
