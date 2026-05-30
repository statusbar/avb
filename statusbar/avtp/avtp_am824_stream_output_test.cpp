// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_am824_stream_output.hpp"

#include "statusbar/test/test.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

using namespace statusbar;
using namespace statusbar::avtp;
using namespace statusbar::tsn;

// ============================================================================
// Am824StreamOutputContext construction and reset
// ============================================================================

TEST(am824_stream_output_ctx, construction)
{
    StreamId sid{};
    Am824StreamOutputContext ctx{sid, Am824SampleRate::rate_48_khz, 2, 2'000'000};
    EXPECT_EQ(ctx.config.channel_count, 2);
    EXPECT_EQ(ctx.config.syt_interval, 8);
    EXPECT_EQ(ctx.config.sample_period_ns, 20833);
    EXPECT_EQ(ctx.presentation_offset_ns, 2'000'000U);
    EXPECT_EQ(ctx.sequence_num, 0);
    EXPECT_EQ(ctx.running_dbc, 0U);
    EXPECT_EQ(ctx.packets_sent, 0U);
}

TEST(am824_stream_output_ctx, reset)
{
    StreamId sid{};
    Am824StreamOutputContext ctx{sid, Am824SampleRate::rate_48_khz, 2, 2'000'000};
    ctx.running_dbc = 100;
    ctx.packets_sent = 10;
    ctx.sequence_num = 5;
    ctx.reset();
    EXPECT_EQ(ctx.running_dbc, 0U);
    EXPECT_EQ(ctx.packets_sent, 0U);
    EXPECT_EQ(ctx.sequence_num, 0);
    EXPECT_EQ(ctx.config.channel_count, 2);
}

// ============================================================================
// build_packet_header
// ============================================================================

TEST(am824_stream_output_ctx, header_first_packet_has_timestamp)
{
    StreamId sid{};
    Am824StreamOutputContext ctx{sid, Am824SampleRate::rate_48_khz, 2, 2'000'000};
    Am824Pdu pdu{};
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);

    // First packet: running_dbc=0, which is a syt_interval boundary (0 % 8 == 0)
    ctx.build_packet_header(pdu, 6, 100'000'000U);

    EXPECT_TRUE(pdu.tv());
    EXPECT_EQ(pdu.sequence_num(), 0);
    EXPECT_EQ(pdu.data_block_count(), 0);
    EXPECT_EQ(ctx.running_dbc, 6U);
    EXPECT_EQ(ctx.sequence_num, 1);
    EXPECT_EQ(ctx.packets_sent, 1U);
    EXPECT_EQ(ctx.timestamp_inserts, 1U);

    // AVTP timestamp should be lower 32 bits of (gptp_now + offset)
    uint64_t const expected_pts = 100'000'000U + 2'000'000U;
    EXPECT_EQ(pdu.avtp_timestamp(), static_cast<uint32_t>(expected_pts & 0xFFFF'FFFFU));
}

TEST(am824_stream_output_ctx, header_no_timestamp_between_boundaries)
{
    StreamId sid{};
    Am824StreamOutputContext ctx{sid, Am824SampleRate::rate_48_khz, 2, 2'000'000};
    Am824Pdu pdu{};
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);

    // First packet consumes DBC 0-5, tv=1 at DBC 0
    ctx.build_packet_header(pdu, 6, 100'000'000U);
    EXPECT_TRUE(pdu.tv());

    // Second packet consumes DBC 6-11, tv=1 at DBC 8
    ctx.build_packet_header(pdu, 6, 100'125'000U);
    EXPECT_TRUE(pdu.tv());  // DBC 8 is in range [6,11]

    // Third packet consumes DBC 12-17, tv=1 at DBC 16
    ctx.build_packet_header(pdu, 6, 100'250'000U);
    EXPECT_TRUE(pdu.tv());  // DBC 16 is in range [12,17]
}

TEST(am824_stream_output_ctx, sequence_number_increments)
{
    StreamId sid{};
    Am824StreamOutputContext ctx{sid, Am824SampleRate::rate_48_khz, 2, 2'000'000};
    Am824Pdu pdu{};
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);

    for (int i = 0; i < 260; ++i) {
        ctx.build_packet_header(pdu, 6, 100'000'000U + static_cast<uint64_t>(i) * 125'000);
    }
    // Sequence wraps at 256
    EXPECT_EQ(ctx.sequence_num, static_cast<uint8_t>(260 & 0xFF));
    EXPECT_EQ(ctx.packets_sent, 260U);
}

TEST(am824_stream_output_ctx, dbc_field_wraps_at_256)
{
    StreamId sid{};
    Am824StreamOutputContext ctx{sid, Am824SampleRate::rate_48_khz, 2, 2'000'000};
    Am824Pdu pdu{};
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);

    // Send enough packets to wrap DBC past 256
    for (int i = 0; i < 50; ++i) {
        ctx.build_packet_header(pdu, 6, 100'000'000U + static_cast<uint64_t>(i) * 125'000);
    }
    // 50 * 6 = 300, running_dbc should be 300
    EXPECT_EQ(ctx.running_dbc, 300U);
    // DBC field in PDU is lower 8 bits
    EXPECT_EQ(pdu.data_block_count(), static_cast<uint8_t>(294 & 0xFF));  // DBC at start of last packet
}

// ============================================================================
// am824_serialize_mbla
// ============================================================================

TEST(am824_stream_output_mbla, stereo_6_samples)
{
    StreamId sid{};
    Am824StreamOutputContext ctx{sid, Am824SampleRate::rate_48_khz, 2, 2'000'000};
    Am824Pdu pdu{};
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);

    std::array<uint8_t, 2 * 6 * 4> payload{};
    float const test_val = 0.5f;

    size_t const bytes =
        am824_serialize_mbla(ctx, pdu, std::span{payload}, 6, 100'000'000U, [&](uint8_t /*channel*/, std::span<float> dest) {
            std::fill(dest.begin(), dest.end(), test_val);
        });

    EXPECT_EQ(bytes, 2U * 6U * 4U);
    EXPECT_EQ(ctx.packets_sent, 1U);
    EXPECT_TRUE(pdu.tv());

    // Verify first quadlet is a valid MBLA sample
    ieee::quadlet_t q{};
    span_load(q, std::span<uint8_t const>{payload.data(), 4});
    uint32_t const quadlet = q;
    uint8_t const label = static_cast<uint8_t>((quadlet >> 24) & 0xFFU);
    EXPECT_EQ(label, AM824_LABEL_MBLA);

    // Verify round-trip: encoded value decodes back to approximately test_val
    float const decoded = am824_sample_to_float(parse_am824_quadlet(quadlet));
    EXPECT_TRUE(std::abs(decoded - test_val) < 0.001f);
}

TEST(am824_stream_output_mbla, variable_sample_counts)
{
    StreamId sid{};
    Am824StreamOutputContext ctx{sid, Am824SampleRate::rate_48_khz, 2, 2'000'000};
    Am824Pdu pdu{};
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);

    std::array<uint8_t, 64 * 32 * 4> payload{};
    auto silent = [](uint8_t, std::span<float> dest) { std::fill(dest.begin(), dest.end(), 0.0f); };

    // Simulate class C packing: 6, 6, 5, 6, 6, 7 samples
    uint8_t const counts[] = {6, 6, 5, 6, 6, 7};
    uint32_t total_dbc = 0;
    for (uint8_t count : counts) {
        size_t const bytes = am824_serialize_mbla(ctx, pdu, std::span{payload}, count, 100'000'000U, silent);
        EXPECT_EQ(bytes, static_cast<size_t>(count) * 2U * 4U);
        total_dbc += count;
    }
    EXPECT_EQ(ctx.running_dbc, total_dbc);
    EXPECT_EQ(ctx.packets_sent, 6U);
}

TEST(am824_stream_output_mbla, insufficient_payload_returns_zero)
{
    StreamId sid{};
    Am824StreamOutputContext ctx{sid, Am824SampleRate::rate_48_khz, 2, 2'000'000};
    Am824Pdu pdu{};
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);

    std::array<uint8_t, 10> too_small{};  // Need 2*6*4=48 bytes
    size_t const bytes = am824_serialize_mbla(ctx, pdu, std::span{too_small}, 6, 100'000'000U, [](uint8_t, std::span<float> dest) {
        std::fill(dest.begin(), dest.end(), 0.0f);
    });
    EXPECT_EQ(bytes, 0U);
    EXPECT_EQ(ctx.packets_sent, 0U);
}

// ============================================================================
// am824_serialize_mixed
// ============================================================================

TEST(am824_stream_output_mixed, audio_plus_midi)
{
    StreamId sid{};
    // 3 channels: 2 audio + 1 MIDI
    Am824StreamOutputContext ctx{sid, Am824SampleRate::rate_48_khz, 3, 2'000'000};
    Am824Pdu pdu{};
    pdu.init(sid, 3, Am824SampleRate::rate_48_khz);

    std::array<Am824ChannelType, 3> types{Am824ChannelType::mbla, Am824ChannelType::mbla, Am824ChannelType::midi};
    std::array<uint8_t, 3 * 2 * 4> payload{};

    size_t const bytes = am824_serialize_mixed(
        ctx,
        pdu,
        std::span{payload},
        2,  // 2 samples per channel
        100'000'000U,
        std::span{types},
        [](uint8_t, std::span<float> dest) { std::fill(dest.begin(), dest.end(), 0.25f); },
        [](uint8_t, uint8_t sample_idx, uint8_t* out) -> uint8_t {
            if (sample_idx == 1) {
                out[0] = 0x90;
                out[1] = 0x3C;
                return 2;
            }
            return 0;  // no MIDI data in first data block
        },
        [](uint8_t, uint8_t, std::array<uint8_t, 3>&) -> uint8_t { return 0; });

    EXPECT_EQ(bytes, 3U * 2U * 4U);
    EXPECT_EQ(ctx.packets_sent, 1U);

    // Verify data block 0, channel 2 = MIDI no-data (label 0x80)
    ieee::quadlet_t q0{};
    span_load(q0, std::span<uint8_t const>{payload.data() + 8, 4});  // offset: (0*3+2)*4=8
    uint8_t const label0 = static_cast<uint8_t>((static_cast<uint32_t>(q0) >> 24) & 0xFFU);
    EXPECT_EQ(label0, AM824_LABEL_RAW_MIDI);  // 0x80, counter=0

    // Verify data block 1, channel 2 = MIDI with 2 bytes (label 0x82)
    ieee::quadlet_t q1{};
    span_load(q1, std::span<uint8_t const>{payload.data() + 20, 4});  // offset: (1*3+2)*4=20
    uint32_t const midi_quadlet = q1;
    uint8_t const label1 = static_cast<uint8_t>((midi_quadlet >> 24) & 0xFFU);
    EXPECT_EQ(label1, 0x82U);  // MIDI counter=2

    // Verify the MIDI bytes
    uint8_t midi_out[3]{};
    uint8_t const n = am824_extract_midi_bytes(midi_quadlet, midi_out);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(midi_out[0], 0x90);
    EXPECT_EQ(midi_out[1], 0x3C);
}

TEST(am824_stream_output_mixed, audio_plus_smpte)
{
    StreamId sid{};
    Am824StreamOutputContext ctx{sid, Am824SampleRate::rate_48_khz, 2, 2'000'000};
    Am824Pdu pdu{};
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);

    std::array<Am824ChannelType, 2> types{Am824ChannelType::mbla, Am824ChannelType::smpte};
    std::array<uint8_t, 2 * 2 * 4> payload{};

    size_t const bytes = am824_serialize_mixed(
        ctx,
        pdu,
        std::span{payload},
        2,
        100'000'000U,
        std::span{types},
        [](uint8_t, std::span<float> dest) { std::fill(dest.begin(), dest.end(), 0.0f); },
        [](uint8_t, uint8_t, uint8_t*) -> uint8_t { return 0; },
        [](uint8_t, uint8_t sample_idx, std::array<uint8_t, 3>& out) -> uint8_t {
            if (sample_idx == 0) {
                out = {0x15, 0x30, 0x01};
                return 1;  // first part
            }
            return 0;
        });

    EXPECT_EQ(bytes, 2U * 2U * 4U);

    // Verify data block 0, channel 1 = SMPTE first part (label 0x89)
    ieee::quadlet_t q{};
    span_load(q, std::span<uint8_t const>{payload.data() + 4, 4});  // offset: (0*2+1)*4=4
    uint32_t const smpte_quadlet = q;
    uint8_t const label = static_cast<uint8_t>((smpte_quadlet >> 24) & 0xFFU);
    EXPECT_EQ(label, 0x89U);  // SMPTE part=1

    std::array<uint8_t, 3> smpte_out{};
    uint8_t const part = am824_extract_smpte_part(smpte_quadlet, smpte_out);
    EXPECT_EQ(part, 1);
    EXPECT_EQ(smpte_out[0], 0x15);
    EXPECT_EQ(smpte_out[1], 0x30);
    EXPECT_EQ(smpte_out[2], 0x01);
}

// ============================================================================
// am824_create_midi_quadlet / am824_create_smpte_quadlet round-trip
// ============================================================================

TEST(am824_stream_output_quadlet, midi_roundtrip)
{
    uint8_t const bytes_in[] = {0x90, 0x3C, 0x7F};
    uint32_t const quadlet = am824_create_midi_quadlet(3, bytes_in);
    uint8_t bytes_out[3]{};
    uint8_t const n = am824_extract_midi_bytes(quadlet, bytes_out);
    EXPECT_EQ(n, 3);
    EXPECT_EQ(bytes_out[0], 0x90);
    EXPECT_EQ(bytes_out[1], 0x3C);
    EXPECT_EQ(bytes_out[2], 0x7F);
}

TEST(am824_stream_output_quadlet, midi_zero_bytes)
{
    uint32_t const quadlet = am824_create_midi_quadlet(0, nullptr);
    uint8_t bytes_out[3]{};
    EXPECT_EQ(am824_extract_midi_bytes(quadlet, bytes_out), 0);
}

TEST(am824_stream_output_quadlet, smpte_roundtrip)
{
    std::array<uint8_t, 3> const payload_in{0x15, 0x30, 0x01};
    uint32_t const quadlet = am824_create_smpte_quadlet(2, payload_in);
    std::array<uint8_t, 3> payload_out{};
    uint8_t const part = am824_extract_smpte_part(quadlet, payload_out);
    EXPECT_EQ(part, 2);
    EXPECT_EQ(payload_out[0], 0x15);
    EXPECT_EQ(payload_out[1], 0x30);
    EXPECT_EQ(payload_out[2], 0x01);
}

TEST_MAIN(statusbar_avtp, avtp_am824_stream_output_test)
