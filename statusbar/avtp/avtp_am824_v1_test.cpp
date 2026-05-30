// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_am824_v1.hpp"

#include "statusbar/avtp/avtp_am824_v1_format.hpp"
#include "statusbar/buffer/span_utils.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <cstring>
#include <string>

using namespace statusbar;
using namespace statusbar::avtp;
using namespace statusbar::ieee;
using namespace statusbar::tsn;

// Compile-time layout check
static_assert(sizeof(AvtpStreamHeaderV1) == AvtpStreamHeaderV1::LENGTH);
static_assert(sizeof(AvtpStreamHeaderV1) == 40);
static_assert(sizeof(Am824V1Pdu) == Am824V1Pdu::HEADER_LENGTH);
static_assert(sizeof(Am824V1Pdu) == 48);

TEST(am824_v1, init_and_accessors)
{
    Am824V1Pdu pdu{};
    StreamId sid(Eui48(0x00, 0x11, 0x22, 0x33, 0x44, 0x55), 0x0001);
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);

    EXPECT_EQ(pdu.stream_header.subtype.get(), AvtpSubtype::iec_61883_iidc);
    EXPECT_TRUE(pdu.stream_header.sv());
    EXPECT_EQ(pdu.stream_header.version(), 1);
    EXPECT_FALSE(pdu.stream_header.mr());
    EXPECT_FALSE(pdu.tv());
    EXPECT_FALSE(pdu.stream_header.tu());
    EXPECT_EQ(pdu.get_sequence_num(), 0U);
    EXPECT_EQ(pdu.stream_id(), sid);
    EXPECT_EQ(pdu.avtp_timestamp(), 0U);
    EXPECT_EQ(pdu.channel_count(), 2);
    EXPECT_EQ(pdu.sample_rate(), Am824SampleRate::rate_48_khz);
    EXPECT_EQ(pdu.data_block_count(), 0U);
    EXPECT_TRUE(pdu.is_valid());
}

TEST(am824_v1, version_is_one)
{
    Am824V1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);

    EXPECT_EQ(pdu.stream_header.version(), 1);
    // sv=1(0x80) | version=1(0x10) = 0x90
    EXPECT_EQ(pdu.stream_header.sv_version_flags.get(), 0x90U);
}

TEST(am824_v1, sequence_num_32bit)
{
    Am824V1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);

    // 32-bit sequence number — can go way beyond 255
    pdu.set_sequence_num(100000);
    EXPECT_EQ(pdu.get_sequence_num(), 100000U);

    pdu.set_sequence_num(0xFFFFFFFF);
    EXPECT_EQ(pdu.get_sequence_num(), 0xFFFFFFFFU);

    // Increment wraps at 2^32
    pdu.increment_sequence_num();
    EXPECT_EQ(pdu.get_sequence_num(), 0U);
}

TEST(am824_v1, timestamp_64bit)
{
    Am824V1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);

    pdu.set_tv(true);

    // 64-bit timestamp — no 4-second rollover
    uint64_t const large_ts = 0x0000'0002'540B'E400ULL;  // 10 billion ns = 10 seconds
    pdu.set_avtp_timestamp(large_ts);
    EXPECT_EQ(pdu.avtp_timestamp(), large_ts);

    // Max value
    pdu.set_avtp_timestamp(0xFFFF'FFFF'FFFF'FFFFULL);
    EXPECT_EQ(pdu.avtp_timestamp(), 0xFFFF'FFFF'FFFF'FFFFULL);
}

TEST(am824_v1, ptp_grandmaster_identity)
{
    Am824V1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);

    ClockIdentity gm(0xAABBCCDDEEFF0011ULL);
    pdu.set_ptp_grandmaster_identity(gm);
    EXPECT_EQ(pdu.get_ptp_grandmaster_identity(), gm);
}

TEST(am824_v1, cip_header)
{
    Am824V1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, 8, Am824SampleRate::rate_96_khz);

    EXPECT_EQ(pdu.channel_count(), 8);
    EXPECT_EQ(pdu.sample_rate(), Am824SampleRate::rate_96_khz);
    EXPECT_EQ(pdu.cip_header.fmt(), AM824_FMT);

    pdu.set_data_block_count(42);
    EXPECT_EQ(pdu.data_block_count(), 42);

    pdu.set_syt_timestamp(0x1234);
    EXPECT_EQ(pdu.syt_timestamp(), 0x1234U);
    EXPECT_TRUE(pdu.cip_header.syt_valid());
}

TEST(am824_v1, dimensions_and_sample_count)
{
    Am824V1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);

    pdu.set_dimensions(6, 2);
    EXPECT_EQ(pdu.channel_count(), 2);
    // stream_data_length = CIP(8) + 6 * 2 * 4 = 56
    EXPECT_EQ(pdu.stream_data_length(), 56);
    EXPECT_EQ(pdu.audio_payload_length(), 48);
    EXPECT_EQ(pdu.sample_count(), 6);
}

TEST(am824_v1, protocol_specific_header)
{
    Am824V1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);

    // After init: tag=1, channel=0, tcode=0xA, sy=0
    EXPECT_EQ(pdu.stream_header.tag(), 1);
    EXPECT_EQ(pdu.stream_header.channel(), 0);
    EXPECT_EQ(pdu.stream_header.tcode(), 0x0A);
    EXPECT_EQ(pdu.stream_header.sy(), 0);
}

TEST(am824_v1, round_trip_serialization)
{
    Am824V1Pdu original{};
    StreamId sid(Eui48(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF), 0x1234);
    original.init(sid, 8, Am824SampleRate::rate_48_khz);
    original.set_tv(true);
    original.set_sequence_num(0x12345678);
    original.set_avtp_timestamp(0x0000'0001'0000'0000ULL);
    ClockIdentity gm(0x0011223344556677ULL);
    original.set_ptp_grandmaster_identity(gm);
    original.set_dimensions(6, 8);
    original.set_data_block_count(100);

    std::array<uint8_t, 64> buf{};
    (void)store_unchecked(std::span<uint8_t>(buf), original);

    // Verify subtype and version bytes
    EXPECT_EQ(buf[0], AvtpSubtype::iec_61883_iidc);
    EXPECT_EQ((buf[1] >> 4) & 0x07, 1);  // version = 1

    // Deserialize
    Am824V1Pdu loaded{};
    (void)load_unchecked(std::span<uint8_t const>(buf), &loaded);
    EXPECT_TRUE(loaded.is_valid());
    EXPECT_EQ(loaded.stream_header.version(), 1);
    EXPECT_EQ(loaded.stream_id(), sid);
    EXPECT_EQ(loaded.get_sequence_num(), 0x12345678U);
    EXPECT_EQ(loaded.avtp_timestamp(), 0x0000'0001'0000'0000ULL);
    EXPECT_EQ(loaded.get_ptp_grandmaster_identity(), gm);
    EXPECT_EQ(loaded.channel_count(), 8);
    EXPECT_EQ(loaded.sample_rate(), Am824SampleRate::rate_48_khz);
    EXPECT_EQ(loaded.sample_count(), 6);
    EXPECT_EQ(loaded.data_block_count(), 100);
}

TEST(am824_v1, parse_valid)
{
    Am824V1Pdu pdu{};
    StreamId sid(Eui48(0x11, 0x22, 0x33, 0x44, 0x55, 0x66), 0x0001);
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);
    pdu.set_dimensions(6, 2);

    std::array<uint8_t, 128> buf{};
    span_store(buf, pdu);

    auto parsed = am824_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->stream_header.version(), 1);
    EXPECT_EQ(parsed->stream_id(), sid);
    EXPECT_EQ(parsed->channel_count(), 2);
}

TEST(am824_v1, parse_truncated)
{
    std::array<uint8_t, 40> buf{};
    buf[0] = AvtpSubtype::iec_61883_iidc;
    buf[1] = 0x90U;  // sv=1, version=1
    auto parsed = am824_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_FALSE(parsed.has_value());
}

TEST(am824_v1, parse_wrong_version)
{
    Am824V1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);
    // Corrupt version to 0
    pdu.stream_header.sv_version_flags = 0x80U;  // sv=1, version=0

    std::array<uint8_t, 48> buf{};
    span_store(buf, pdu);
    auto parsed = am824_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_FALSE(parsed.has_value());
}

TEST(am824_v1, get_audio_payload)
{
    std::array<uint8_t, 80> buf{};
    buf[0] = AvtpSubtype::iec_61883_iidc;
    buf[48] = 0xBE;  // first byte of audio payload

    auto payload = am824_v1_get_audio_payload(std::span<uint8_t const>(buf));
    EXPECT_EQ(payload.size(), 32U);
    EXPECT_EQ(payload[0], 0xBEU);

    auto empty = am824_v1_get_audio_payload(std::span<uint8_t const>(buf.data(), Am824V1Pdu::HEADER_LENGTH));
    EXPECT_TRUE(empty.empty());
}

TEST(am824_v1, format_to_output)
{
    Am824V1Pdu pdu{};
    StreamId sid(Eui48(0x00, 0x11, 0x22, 0x33, 0x44, 0x55), 0x0001);
    pdu.init(sid, 2, Am824SampleRate::rate_48_khz);
    pdu.set_tv(true);
    pdu.set_avtp_timestamp(1000);
    pdu.set_dimensions(6, 2);

    std::string result;
    format_to(std::back_inserter(result), pdu);
    EXPECT_TRUE(result.find("AM824(v1)") != std::string::npos);
    EXPECT_TRUE(result.find("channels=2") != std::string::npos);
    EXPECT_TRUE(result.find("48 kHz") != std::string::npos);
}

TEST_MAIN(statusbar_avtp, avtp_am824_v1_test)
