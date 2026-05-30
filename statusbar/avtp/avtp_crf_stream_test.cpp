// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_crf_stream_input.hpp"
#include "statusbar/avtp/avtp_crf_stream_output.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace statusbar;
using namespace statusbar::avtp;
using namespace statusbar::tsn;

// ============================================================================
// CrfStreamInputContext construction and reset
// ============================================================================

TEST(crf_stream_input_ctx, construction)
{
    CrfStreamInputContext ctx{CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1};
    EXPECT_EQ(ctx.base_frequency, 48000U);
    EXPECT_EQ(ctx.timestamp_interval, 1);
    EXPECT_FALSE(ctx.has_valid_data);
    EXPECT_EQ(ctx.packets_received, 0U);
}

TEST(crf_stream_input_ctx, default_construction)
{
    CrfStreamInputContext ctx{};
    EXPECT_EQ(ctx.base_frequency, 0U);
    EXPECT_FALSE(ctx.has_valid_data);
}

TEST(crf_stream_input_ctx, reset)
{
    CrfStreamInputContext ctx{CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1};
    ctx.packets_received = 10;
    ctx.has_valid_data = true;
    ctx.last_timestamp_ns = 12345;
    ctx.reset();
    EXPECT_EQ(ctx.packets_received, 0U);
    EXPECT_FALSE(ctx.has_valid_data);
    EXPECT_EQ(ctx.last_timestamp_ns, 0U);
    EXPECT_EQ(ctx.base_frequency, 48000U);  // config preserved
}

TEST(crf_stream_input_ctx, nominal_period)
{
    CrfStreamInputContext ctx{CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1};
    // 1e9 / 48000 ≈ 20833.33
    EXPECT_TRUE(std::abs(ctx.nominal_period_ns() - 20833.33) < 1.0);
}

TEST(crf_stream_input_ctx, nominal_period_with_pull)
{
    CrfStreamInputContext ctx{CrfType::audio_sample, 48000, CrfPull::multiply_1_div_1001, 1};
    double const expected = 1'000'000'000.0 / (48000.0 / 1.001);
    EXPECT_TRUE(std::abs(ctx.nominal_period_ns() - expected) < 0.1);
}

// ============================================================================
// CRF input — sequence gap detection
// ============================================================================

TEST(crf_stream_input_seq, no_gap)
{
    CrfStreamInputContext ctx{CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1};
    ctx.update_sequence_num(0);
    ++ctx.packets_received;
    ctx.update_sequence_num(1);
    ++ctx.packets_received;
    ctx.update_sequence_num(2);
    EXPECT_EQ(ctx.sequence_gaps, 0U);
}

TEST(crf_stream_input_seq, gap_detected)
{
    CrfStreamInputContext ctx{CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1};
    ctx.update_sequence_num(0);
    ++ctx.packets_received;
    ctx.update_sequence_num(5);
    EXPECT_EQ(ctx.sequence_gaps, 1U);
}

// ============================================================================
// CRF input — process_packet with timestamp callback
// ============================================================================

static auto build_crf_timestamps(std::vector<uint64_t> const& timestamps) -> std::vector<uint8_t>
{
    std::vector<uint8_t> data(timestamps.size() * 8);
    for (size_t i = 0; i < timestamps.size(); ++i) {
        (void)crf_set_timestamp(std::span{data}, i, timestamps[i]);
    }
    return data;
}

TEST(crf_stream_input_process, single_packet_6_timestamps)
{
    CrfStreamInputContext ctx{CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1};

    CrfPdu pdu{};
    StreamId sid{};
    pdu.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 1, 6);
    pdu.set_sequence_num(0);

    // 6 timestamps: 1ms apart (for illustration)
    std::vector<uint64_t> const expected_ts = {
        1'000'000'000ULL, 1'000'020'833ULL, 1'000'041'666ULL, 1'000'062'500ULL, 1'000'083'333ULL, 1'000'104'166ULL};
    auto ts_data = build_crf_timestamps(expected_ts);

    std::vector<uint64_t> received_ts;
    ctx.process_packet(pdu, std::span{ts_data}, [&](uint64_t ts, uint16_t index) {
        EXPECT_EQ(index, static_cast<uint16_t>(received_ts.size()));
        received_ts.push_back(ts);
    });

    EXPECT_EQ(received_ts.size(), 6U);
    EXPECT_EQ(ctx.packets_received, 1U);
    EXPECT_EQ(ctx.total_timestamps_received, 6U);
    EXPECT_TRUE(ctx.has_valid_data);
    EXPECT_EQ(ctx.first_timestamp_ns, expected_ts[0]);
    EXPECT_EQ(ctx.last_timestamp_ns, expected_ts[5]);

    for (size_t i = 0; i < expected_ts.size(); ++i) {
        EXPECT_EQ(received_ts[i], expected_ts[i]);
    }
}

TEST(crf_stream_input_process, media_clock_restart_detected)
{
    CrfStreamInputContext ctx{CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1};

    CrfPdu pdu{};
    StreamId sid{};
    pdu.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 1, 1);
    pdu.set_mr(true);
    pdu.set_sequence_num(0);

    auto ts_data = build_crf_timestamps({1'000'000'000ULL});

    ctx.process_packet(pdu, std::span{ts_data}, [](uint64_t, uint16_t) {});
    EXPECT_TRUE(ctx.media_clock_restarted);
}

TEST(crf_stream_input_process, config_from_first_packet)
{
    CrfStreamInputContext ctx{};  // default constructed, no config

    CrfPdu pdu{};
    StreamId sid{};
    pdu.init_audio_sample(sid, 96000, CrfPull::multiply_1001, 8, 3);
    pdu.set_sequence_num(0);

    auto ts_data = build_crf_timestamps({100ULL, 200ULL, 300ULL});

    ctx.process_packet(pdu, std::span{ts_data}, [](uint64_t, uint16_t) {});
    EXPECT_EQ(ctx.base_frequency, 96000U);
    EXPECT_EQ(ctx.timestamp_interval, 8);
}

// ============================================================================
// CrfStreamOutputContext construction and reset
// ============================================================================

TEST(crf_stream_output_ctx, construction)
{
    StreamId sid{};
    CrfStreamOutputContext ctx{sid, CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1, 6};
    EXPECT_EQ(ctx.base_frequency, 48000U);
    EXPECT_EQ(ctx.timestamp_interval, 1);
    EXPECT_EQ(ctx.timestamps_per_packet, 6);
    EXPECT_EQ(ctx.packets_sent, 0U);
    EXPECT_FALSE(ctx.started);
}

TEST(crf_stream_output_ctx, reset)
{
    StreamId sid{};
    CrfStreamOutputContext ctx{sid, CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1, 6};
    ctx.packets_sent = 10;
    ctx.started = true;
    ctx.next_timestamp_ns = 12345;
    ctx.reset();
    EXPECT_EQ(ctx.packets_sent, 0U);
    EXPECT_FALSE(ctx.started);
    EXPECT_EQ(ctx.next_timestamp_ns, 0U);
}

TEST(crf_stream_output_ctx, nominal_period)
{
    StreamId sid{};
    CrfStreamOutputContext ctx{sid, CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1, 6};
    EXPECT_TRUE(std::abs(ctx.nominal_period_ns() - 20833.33) < 1.0);
}

TEST(crf_stream_output_ctx, timestamp_spacing)
{
    StreamId sid{};
    // timestamp_interval=8: spacing = 8 * period
    CrfStreamOutputContext ctx{sid, CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 8, 6};
    double const expected = 8.0 * 1'000'000'000.0 / 48000.0;
    EXPECT_TRUE(std::abs(ctx.timestamp_spacing_ns() - expected) < 1.0);
}

// ============================================================================
// CRF output — build_packet
// ============================================================================

TEST(crf_stream_output_build, first_packet)
{
    StreamId sid{};
    CrfStreamOutputContext ctx{sid, CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1, 6};
    CrfPdu pdu{};
    std::array<uint8_t, 6 * 8> ts_data{};

    size_t const bytes = ctx.build_packet(pdu, std::span{ts_data}, 1'000'000'000ULL);

    EXPECT_EQ(bytes, 48U);
    EXPECT_EQ(ctx.packets_sent, 1U);
    EXPECT_TRUE(ctx.started);
    EXPECT_EQ(pdu.get_sequence_num(), 0);
    EXPECT_EQ(pdu.timestamp_count(), 6);
    EXPECT_EQ(pdu.base_frequency(), 48000U);

    // First timestamp should be anchored at gptp_now
    auto const first = crf_get_timestamp(std::span<uint8_t const>{ts_data}, 0);
    EXPECT_TRUE(first.has_value());
    EXPECT_EQ(*first, 1'000'000'000ULL);

    // Timestamps should be evenly spaced
    uint64_t const spacing = static_cast<uint64_t>(std::round(1'000'000'000.0 / 48000.0));
    for (size_t i = 1; i < 6; ++i) {
        auto const ts = crf_get_timestamp(std::span<uint8_t const>{ts_data}, i);
        EXPECT_TRUE(ts.has_value());
        EXPECT_EQ(*ts, 1'000'000'000ULL + i * spacing);
    }
}

TEST(crf_stream_output_build, sequential_packets_continuous)
{
    StreamId sid{};
    CrfStreamOutputContext ctx{sid, CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1, 6};
    CrfPdu pdu{};
    std::array<uint8_t, 6 * 8> ts_data{};

    // Packet 1
    ctx.build_packet(pdu, std::span{ts_data}, 1'000'000'000ULL);
    auto const last_of_pkt1 = crf_get_timestamp(std::span<uint8_t const>{ts_data}, 5);

    // Packet 2
    ctx.build_packet(pdu, std::span{ts_data}, 1'000'125'000ULL);
    auto const first_of_pkt2 = crf_get_timestamp(std::span<uint8_t const>{ts_data}, 0);

    // First timestamp of packet 2 should be exactly one spacing after last of packet 1
    EXPECT_TRUE(last_of_pkt1.has_value());
    EXPECT_TRUE(first_of_pkt2.has_value());
    uint64_t const spacing = static_cast<uint64_t>(std::round(1'000'000'000.0 / 48000.0));
    EXPECT_EQ(*first_of_pkt2, *last_of_pkt1 + spacing);

    EXPECT_EQ(ctx.packets_sent, 2U);
    EXPECT_EQ(ctx.total_timestamps_sent, 12U);
    EXPECT_EQ(pdu.get_sequence_num(), 1);
}

TEST(crf_stream_output_build, insufficient_buffer)
{
    StreamId sid{};
    CrfStreamOutputContext ctx{sid, CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1, 6};
    CrfPdu pdu{};
    std::array<uint8_t, 10> too_small{};  // need 48 bytes

    size_t const bytes = ctx.build_packet(pdu, std::span{too_small}, 1'000'000'000ULL);
    EXPECT_EQ(bytes, 0U);
    EXPECT_EQ(ctx.packets_sent, 0U);
}

TEST(crf_stream_output_build, sequence_wraps)
{
    StreamId sid{};
    CrfStreamOutputContext ctx{sid, CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1, 1};
    CrfPdu pdu{};
    std::array<uint8_t, 8> ts_data{};

    for (int i = 0; i < 260; ++i) {
        ctx.build_packet(pdu, std::span{ts_data}, 1'000'000'000ULL + static_cast<uint64_t>(i) * 125'000);
    }
    EXPECT_EQ(ctx.sequence_num, static_cast<uint8_t>(260 & 0xFF));
}

// ============================================================================
// Roundtrip: output -> input
// ============================================================================

TEST(crf_stream_roundtrip, output_to_input)
{
    StreamId sid{};
    CrfStreamOutputContext out_ctx{sid, CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1, 6};
    CrfPdu pdu{};
    std::array<uint8_t, 6 * 8> ts_data{};

    out_ctx.build_packet(pdu, std::span{ts_data}, 1'000'000'000ULL);

    // Feed into input context
    CrfStreamInputContext in_ctx{};
    std::vector<uint64_t> received;
    in_ctx.process_packet(pdu, std::span<uint8_t const>{ts_data}, [&](uint64_t ts, uint16_t) { received.push_back(ts); });

    EXPECT_EQ(received.size(), 6U);
    EXPECT_EQ(in_ctx.packets_received, 1U);
    EXPECT_TRUE(in_ctx.has_valid_data);
    EXPECT_EQ(in_ctx.base_frequency, 48000U);
    EXPECT_EQ(in_ctx.timestamp_interval, 1);

    // Verify timestamps match what was generated
    uint64_t const spacing = static_cast<uint64_t>(std::round(1'000'000'000.0 / 48000.0));
    for (size_t i = 0; i < 6; ++i) {
        EXPECT_EQ(received[i], 1'000'000'000ULL + i * spacing);
    }
}

// ============================================================================
// Decoder safety: malformed CRF packets must be tolerated without overrun
// or division-by-zero.
// ============================================================================

TEST(crf_stream_input_safety, base_frequency_zero_yields_zero_period)
{
    // Per nominal_period_ns(): freq <= 0 must short-circuit to 0.0
    // instead of dividing by zero.
    CrfStreamInputContext ctx{CrfType::audio_sample, 0, CrfPull::multiply_1_0, 1};
    EXPECT_TRUE(ctx.nominal_period_ns() == 0.0);
}

TEST(crf_stream_input_safety, timestamp_count_exceeds_payload)
{
    // PDU advertises 6 timestamps but only 2 timestamps' worth of bytes
    // are provided. process_packet must clamp to min(count, available)
    // and not read past the end of the span.
    CrfStreamInputContext ctx{CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1};
    CrfPdu pdu{};
    StreamId sid{};
    pdu.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 1, 6);  // declares 6 timestamps
    pdu.set_sequence_num(0);

    auto ts_data = build_crf_timestamps({100ULL, 200ULL});  // only 2 actually present

    size_t callbacks = 0;
    ctx.process_packet(pdu, std::span{ts_data}, [&](uint64_t, uint16_t) { ++callbacks; });
    EXPECT_EQ(callbacks, size_t{2});
    EXPECT_EQ(ctx.last_timestamp_ns, 200ULL);
}

TEST(crf_stream_input_safety, empty_timestamp_payload_no_callbacks)
{
    CrfStreamInputContext ctx{CrfType::audio_sample, 48000, CrfPull::multiply_1_0, 1};
    CrfPdu pdu{};
    StreamId sid{};
    pdu.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 1, 4);  // declares 4
    pdu.set_sequence_num(0);

    std::array<uint8_t, 0> empty{};
    bool called = false;
    ctx.process_packet(pdu, std::span<uint8_t const>{empty}, [&](uint64_t, uint16_t) { called = true; });
    EXPECT_FALSE(called);
}

TEST_MAIN(statusbar_avtp, avtp_crf_stream_test)
