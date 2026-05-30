// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_aaf_stream_output.hpp"

#include "statusbar/avtp/avtp_aaf_stream_input.hpp"
#include "statusbar/test/test.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

using namespace statusbar;
using namespace statusbar::avtp;
using namespace statusbar::tsn;

// ============================================================================
// AafStreamOutputContext construction and reset
// ============================================================================

TEST(aaf_stream_output_ctx, construction)
{
    StreamId sid{};
    AafStreamOutputContext ctx{sid, AafFormat::int_24bit, AafSampleRate::rate_48_khz, 2, 24, 2'000'000};
    EXPECT_EQ(ctx.channel_count, 2);
    EXPECT_EQ(ctx.bit_depth, 24);
    EXPECT_EQ(ctx.sample_period_ns, 20833U);
    EXPECT_EQ(ctx.presentation_offset_ns, 2'000'000U);
    EXPECT_EQ(ctx.sequence_num, 0);
    EXPECT_EQ(ctx.packets_sent, 0U);
}

TEST(aaf_stream_output_ctx, reset)
{
    StreamId sid{};
    AafStreamOutputContext ctx{sid, AafFormat::int_24bit, AafSampleRate::rate_48_khz, 2, 24, 2'000'000};
    ctx.running_sample_count = 100;
    ctx.packets_sent = 10;
    ctx.sequence_num = 5;
    ctx.reset();
    EXPECT_EQ(ctx.running_sample_count, 0U);
    EXPECT_EQ(ctx.packets_sent, 0U);
    EXPECT_EQ(ctx.sequence_num, 0);
}

// ============================================================================
// build_packet_header
// ============================================================================

TEST(aaf_stream_output_ctx, header_has_timestamp)
{
    StreamId sid{};
    AafStreamOutputContext ctx{sid, AafFormat::int_24bit, AafSampleRate::rate_48_khz, 2, 24, 2'000'000};
    AafPdu pdu{};

    ctx.build_packet_header(pdu, 6, 100'000'000U);

    EXPECT_TRUE(pdu.tv());
    EXPECT_FALSE(pdu.sp());
    EXPECT_EQ(pdu.get_sequence_num(), 0);
    EXPECT_EQ(pdu.channels_per_frame(), 2);
    EXPECT_EQ(pdu.sample_count(), 6);
    EXPECT_EQ(ctx.packets_sent, 1U);
    EXPECT_EQ(ctx.sequence_num, 1);

    // AVTP timestamp = lower 32 bits of (gptp_now + offset)
    uint64_t const expected_pts = 100'000'000U + 2'000'000U;
    EXPECT_EQ(pdu.get_avtp_timestamp(), static_cast<uint32_t>(expected_pts & 0xFFFF'FFFFU));
}

TEST(aaf_stream_output_ctx, sequence_wraps)
{
    StreamId sid{};
    AafStreamOutputContext ctx{sid, AafFormat::int_24bit, AafSampleRate::rate_48_khz, 2, 24, 2'000'000};
    AafPdu pdu{};

    for (int i = 0; i < 260; ++i) {
        ctx.build_packet_header(pdu, 6, 100'000'000U + static_cast<uint64_t>(i) * 125'000);
    }
    EXPECT_EQ(ctx.sequence_num, static_cast<uint8_t>(260 & 0xFF));
    EXPECT_EQ(ctx.packets_sent, 260U);
}

// ============================================================================
// aaf_stream_serialize — int24 format
// ============================================================================

TEST(aaf_stream_output_ser, int24_stereo_6_samples)
{
    StreamId sid{};
    AafStreamOutputContext ctx{sid, AafFormat::int_24bit, AafSampleRate::rate_48_khz, 2, 24, 2'000'000};
    AafPdu pdu{};

    std::array<uint8_t, 2 * 6 * 3> payload{};
    float const test_val = 0.5f;

    size_t const bytes = aaf_stream_serialize(ctx, pdu, std::span{payload}, 6, 100'000'000U, [&](uint8_t, std::span<float> dest) {
        std::fill(dest.begin(), dest.end(), test_val);
    });

    EXPECT_EQ(bytes, 2U * 6U * 3U);
    EXPECT_EQ(ctx.packets_sent, 1U);
    EXPECT_TRUE(pdu.tv());

    // Verify first sample decodes back
    float const decoded = decode_aaf_sample(std::span<uint8_t const>{payload}, 0, AafFormat::int_24bit);
    EXPECT_TRUE(std::abs(decoded - test_val) < 0.001f);
}

// ============================================================================
// aaf_stream_serialize — float32 format
// ============================================================================

TEST(aaf_stream_output_ser, float32_mono_8_samples)
{
    StreamId sid{};
    AafStreamOutputContext ctx{sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 1, 32, 2'000'000};
    AafPdu pdu{};

    std::array<uint8_t, 1 * 8 * 4> payload{};
    float const test_val = -0.75f;

    size_t const bytes = aaf_stream_serialize(ctx, pdu, std::span{payload}, 8, 100'000'000U, [&](uint8_t, std::span<float> dest) {
        std::fill(dest.begin(), dest.end(), test_val);
    });

    EXPECT_EQ(bytes, 1U * 8U * 4U);

    // float32 should be bit-exact
    float const decoded = decode_aaf_sample(std::span<uint8_t const>{payload}, 0, AafFormat::float_32bit);
    EXPECT_TRUE(decoded == test_val);
}

// ============================================================================
// aaf_stream_serialize — int16 format
// ============================================================================

TEST(aaf_stream_output_ser, int16_stereo_6_samples)
{
    StreamId sid{};
    AafStreamOutputContext ctx{sid, AafFormat::int_16bit, AafSampleRate::rate_48_khz, 2, 16, 2'000'000};
    AafPdu pdu{};

    std::array<uint8_t, 2 * 6 * 2> payload{};

    size_t const bytes = aaf_stream_serialize(ctx, pdu, std::span{payload}, 6, 100'000'000U, [](uint8_t, std::span<float> dest) {
        std::fill(dest.begin(), dest.end(), 0.25f);
    });

    EXPECT_EQ(bytes, 2U * 6U * 2U);

    float const decoded = decode_aaf_sample(std::span<uint8_t const>{payload}, 0, AafFormat::int_16bit);
    EXPECT_TRUE(std::abs(decoded - 0.25f) < 0.01f);
}

// ============================================================================
// Variable sample counts
// ============================================================================

TEST(aaf_stream_output_ser, variable_sample_counts)
{
    StreamId sid{};
    AafStreamOutputContext ctx{sid, AafFormat::int_24bit, AafSampleRate::rate_48_khz, 2, 24, 2'000'000};
    AafPdu pdu{};

    std::array<uint8_t, 64 * 256 * 4> payload{};
    auto silent = [](uint8_t, std::span<float> dest) { std::fill(dest.begin(), dest.end(), 0.0f); };

    uint16_t const counts[] = {6, 6, 5, 6, 6, 7};
    uint32_t total = 0;
    for (uint16_t count : counts) {
        size_t const bytes = aaf_stream_serialize(ctx, pdu, std::span{payload}, count, 100'000'000U, silent);
        EXPECT_EQ(bytes, static_cast<size_t>(count) * 2U * 3U);
        total += count;
    }
    EXPECT_EQ(ctx.running_sample_count, total);
    EXPECT_EQ(ctx.packets_sent, 6U);
}

TEST(aaf_stream_output_ser, insufficient_payload_returns_zero)
{
    StreamId sid{};
    AafStreamOutputContext ctx{sid, AafFormat::int_24bit, AafSampleRate::rate_48_khz, 2, 24, 2'000'000};
    AafPdu pdu{};

    std::array<uint8_t, 10> too_small{};  // Need 2*6*3=36 bytes
    size_t const bytes = aaf_stream_serialize(ctx, pdu, std::span{too_small}, 6, 100'000'000U, [](uint8_t, std::span<float> dest) {
        std::fill(dest.begin(), dest.end(), 0.0f);
    });
    EXPECT_EQ(bytes, 0U);
    EXPECT_EQ(ctx.packets_sent, 0U);
}

// ============================================================================
// Roundtrip: serialize -> deserialize
// ============================================================================

TEST(aaf_stream_roundtrip, int24_encode_decode)
{
    StreamId sid{};
    AafStreamOutputContext out_ctx{sid, AafFormat::int_24bit, AafSampleRate::rate_48_khz, 2, 24, 2'000'000};
    AafPdu pdu{};

    std::array<uint8_t, 2 * 6 * 3> payload{};
    float const ch0_val = 0.3f;
    float const ch1_val = -0.6f;

    aaf_stream_serialize(out_ctx, pdu, std::span{payload}, 6, 100'000'000U, [&](uint8_t ch, std::span<float> dest) {
        std::fill(dest.begin(), dest.end(), ch == 0 ? ch0_val : ch1_val);
    });

    // Now deserialize
    AafStreamInputContext in_ctx{AafFormat::int_24bit, AafSampleRate::rate_48_khz, 2, 24};

    float received_ch0 = 0;
    float received_ch1 = 0;

    aaf_stream_deserialize(
        in_ctx,
        pdu,
        std::span<uint8_t const>{payload},
        100'000'000U,
        [&](uint8_t ch, std::span<float> samples, uint64_t, uint64_t) {
            EXPECT_EQ(samples.size(), 6U);
            if (ch == 0) {
                received_ch0 = samples[0];
            }
            if (ch == 1) {
                received_ch1 = samples[0];
            }
        });

    // 24-bit quantization: tolerance ~1/2^23 ≈ 1.2e-7
    EXPECT_TRUE(std::abs(received_ch0 - ch0_val) < 0.001f);
    EXPECT_TRUE(std::abs(received_ch1 - ch1_val) < 0.001f);
}

TEST(aaf_stream_roundtrip, float32_bit_exact)
{
    StreamId sid{};
    AafStreamOutputContext out_ctx{sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 1, 32, 2'000'000};
    AafPdu pdu{};

    std::array<uint8_t, 1 * 4 * 4> payload{};
    float const values[] = {0.1f, -0.2f, 0.999f, -1.0f};

    aaf_stream_serialize(out_ctx, pdu, std::span{payload}, 4, 100'000'000U, [&](uint8_t, std::span<float> dest) {
        for (size_t i = 0; i < 4; ++i) {
            dest[i] = values[i];
        }
    });

    AafStreamInputContext in_ctx{AafFormat::float_32bit, AafSampleRate::rate_48_khz, 1, 32};
    aaf_stream_deserialize(
        in_ctx, pdu, std::span<uint8_t const>{payload}, 100'000'000U, [&](uint8_t, std::span<float> samples, uint64_t, uint64_t) {
            EXPECT_EQ(samples.size(), 4U);
            for (size_t i = 0; i < 4; ++i) {
                EXPECT_TRUE(samples[i] == values[i]);
            }
        });
}

TEST_MAIN(statusbar_avtp, avtp_aaf_stream_output_test)
