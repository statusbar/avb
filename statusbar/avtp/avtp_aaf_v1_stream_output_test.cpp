// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_aaf_v1_stream_output.hpp"

#include "statusbar/test/test.hpp"

#include <array>
#include <cstdint>

using namespace statusbar;
using namespace statusbar::avtp;
using namespace statusbar::tsn;

// ============================================================================
// AafV1StreamOutputContext construction and reset
// ============================================================================

TEST(aaf_v1_stream_output_ctx, construction)
{
    StreamId sid(ieee::Eui48(0x00, 0x11, 0x22, 0x33, 0x44, 0x55), 0x0001);
    ClockIdentity gm(0x0011223344556677ULL);
    AafV1StreamOutputContext ctx{sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32, 2'000'000, gm};

    EXPECT_EQ(ctx.channel_count, 2);
    EXPECT_EQ(ctx.bit_depth, 32);
    EXPECT_EQ(ctx.sample_period_ns, 20833U);
    EXPECT_EQ(ctx.presentation_offset_ns, 2'000'000U);
    EXPECT_EQ(ctx.grandmaster_identity, gm);
    EXPECT_EQ(ctx.sequence_num, 0U);
    EXPECT_EQ(ctx.packets_sent, 0U);
}

TEST(aaf_v1_stream_output_ctx, reset)
{
    StreamId sid{};
    AafV1StreamOutputContext ctx{sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32, 2'000'000};
    ctx.sequence_num = 100;
    ctx.packets_sent = 50;
    ctx.running_sample_count = 300;
    ctx.reset();
    EXPECT_EQ(ctx.sequence_num, 0U);
    EXPECT_EQ(ctx.packets_sent, 0U);
    EXPECT_EQ(ctx.running_sample_count, 0U);
}

// ============================================================================
// Header building
// ============================================================================

TEST(aaf_v1_stream_output_hdr, builds_v1_header)
{
    StreamId sid(ieee::Eui48(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF), 0x1234);
    ClockIdentity gm(0x1122334455667788ULL);
    AafV1StreamOutputContext ctx{sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32, 2'000'000, gm};

    AafV1Pdu pdu{};
    ctx.build_packet_header(pdu, 6, 1'000'000'000ULL);

    EXPECT_TRUE(pdu.is_valid());
    EXPECT_EQ(pdu.version(), 1);
    EXPECT_TRUE(pdu.sv());
    EXPECT_TRUE(pdu.tv());
    EXPECT_FALSE(pdu.sp());
    EXPECT_EQ(pdu.stream_id(), sid);
    EXPECT_EQ(pdu.get_sequence_num(), 0U);
    EXPECT_EQ(pdu.channels_per_frame(), 2);
    EXPECT_EQ(pdu.sample_count(), 6);
    EXPECT_EQ(pdu.get_ptp_grandmaster_identity(), gm);

    // Timestamp = gptp_now + offset (full 64-bit)
    EXPECT_EQ(pdu.get_avtp_timestamp(), 1'000'000'000ULL + 2'000'000ULL);
}

TEST(aaf_v1_stream_output_hdr, sequence_increments)
{
    StreamId sid{};
    AafV1StreamOutputContext ctx{sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32, 2'000'000};

    AafV1Pdu pdu{};
    ctx.build_packet_header(pdu, 6, 1'000'000'000ULL);
    EXPECT_EQ(pdu.get_sequence_num(), 0U);

    ctx.build_packet_header(pdu, 6, 1'000'125'000ULL);
    EXPECT_EQ(pdu.get_sequence_num(), 1U);

    ctx.build_packet_header(pdu, 6, 1'000'250'000ULL);
    EXPECT_EQ(pdu.get_sequence_num(), 2U);

    EXPECT_EQ(ctx.packets_sent, 3U);
}

TEST(aaf_v1_stream_output_hdr, sequence_beyond_8bit)
{
    StreamId sid{};
    AafV1StreamOutputContext ctx{sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32, 2'000'000};

    // Manually set sequence to 255 — V0 would wrap to 0, V1 continues
    ctx.sequence_num = 255;
    AafV1Pdu pdu{};
    ctx.build_packet_header(pdu, 6, 1'000'000'000ULL);
    EXPECT_EQ(pdu.get_sequence_num(), 255U);

    ctx.build_packet_header(pdu, 6, 1'000'125'000ULL);
    EXPECT_EQ(pdu.get_sequence_num(), 256U);  // V0 would be 0 here
}

// ============================================================================
// Full serialize round-trip
// ============================================================================

TEST(aaf_v1_stream_output_ser, float32_stereo)
{
    StreamId sid(ieee::Eui48(0x00, 0x11, 0x22, 0x33, 0x44, 0x55), 0x0001);
    AafV1StreamOutputContext ctx{sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32, 2'000'000};

    AafV1Pdu pdu{};
    std::array<uint8_t, 48> payload{};  // 6 samples * 2 ch * 4 bytes = 48

    size_t const written =
        aaf_v1_stream_serialize(ctx, pdu, std::span<uint8_t>(payload), 6, 1'000'000'000ULL, [](uint8_t ch, std::span<float> dest) {
            for (size_t i = 0; i < dest.size(); ++i) {
                dest[i] = static_cast<float>(ch) * 0.1F + static_cast<float>(i) * 0.01F;
            }
        });

    EXPECT_EQ(written, 48U);
    EXPECT_TRUE(pdu.is_valid());
    EXPECT_EQ(pdu.version(), 1);
    EXPECT_EQ(ctx.packets_sent, 1U);
    EXPECT_EQ(ctx.running_sample_count, 6U);
}

TEST(aaf_v1_stream_output_ser, invalid_params_return_zero)
{
    StreamId sid{};
    AafV1StreamOutputContext ctx{sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32, 2'000'000};

    AafV1Pdu pdu{};
    std::array<uint8_t, 4> small_buf{};

    // Buffer too small
    size_t const written =
        aaf_v1_stream_serialize(ctx, pdu, std::span<uint8_t>(small_buf), 6, 1'000'000'000ULL, [](uint8_t, std::span<float>) {});

    EXPECT_EQ(written, 0U);
}

TEST_MAIN(statusbar_avtp, avtp_aaf_v1_stream_output_test)
