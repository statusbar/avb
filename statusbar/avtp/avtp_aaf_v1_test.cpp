// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_aaf_v1.hpp"

#include "statusbar/avtp/avtp_aaf_v1_format.hpp"
#include "statusbar/buffer/span_utils.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <string>

using namespace statusbar;
using namespace statusbar::avtp;
using namespace statusbar::ieee;
using namespace statusbar::tsn;

// Compile-time layout check
static_assert(sizeof(AafV1Pdu) == AafV1Pdu::HEADER_LENGTH);
static_assert(sizeof(AafV1Pdu) == 40);

TEST(aaf_v1, init_and_accessors)
{
    AafV1Pdu pdu{};
    StreamId sid(Eui48(0x00, 0x11, 0x22, 0x33, 0x44, 0x55), 0x0001);
    pdu.init(sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32);

    EXPECT_EQ(pdu.subtype.get(), AvtpSubtype::aaf);
    EXPECT_TRUE(pdu.sv());
    EXPECT_EQ(pdu.version(), 1);
    EXPECT_FALSE(pdu.mr());
    EXPECT_FALSE(pdu.tv());
    EXPECT_FALSE(pdu.tu());
    EXPECT_EQ(pdu.get_sequence_num(), 0U);
    EXPECT_EQ(pdu.stream_id(), sid);
    EXPECT_EQ(pdu.get_avtp_timestamp(), 0U);
    EXPECT_EQ(pdu.get_format(), AafFormat::float_32bit);
    EXPECT_EQ(pdu.nsr(), AafSampleRate::rate_48_khz);
    EXPECT_EQ(pdu.channels_per_frame(), 2);
    EXPECT_EQ(pdu.get_bit_depth(), 32);
    EXPECT_EQ(pdu.get_stream_data_length(), 0);
    EXPECT_FALSE(pdu.sp());
    EXPECT_EQ(pdu.evt(), 0);
    EXPECT_TRUE(pdu.is_valid());
}

TEST(aaf_v1, version_is_one)
{
    AafV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, AafFormat::int_24bit, AafSampleRate::rate_48_khz, 8, 24);

    // Version must be 1, not 0
    EXPECT_EQ(pdu.version(), 1);
    EXPECT_TRUE(pdu.is_valid());

    // Verify sv_version_flags byte: sv=1(0x80) | version=1(0x10) = 0x90
    EXPECT_EQ(pdu.sv_version_flags.get(), 0x90U);
}

TEST(aaf_v1, sequence_num_32bit)
{
    AafV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, AafFormat::int_16bit, AafSampleRate::rate_48_khz, 2, 16);

    // 32-bit sequence number — can go way beyond 255
    pdu.set_sequence_num(100000);
    EXPECT_EQ(pdu.get_sequence_num(), 100000U);

    pdu.set_sequence_num(0xFFFFFFFF);
    EXPECT_EQ(pdu.get_sequence_num(), 0xFFFFFFFFU);

    // Increment wraps at 2^32
    pdu.increment_sequence_num();
    EXPECT_EQ(pdu.get_sequence_num(), 0U);
}

TEST(aaf_v1, timestamp_64bit)
{
    AafV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32);

    pdu.set_tv(true);

    // 64-bit timestamp — no 4-second rollover
    uint64_t const large_ts = 0x0000'0002'540B'E400ULL;  // 10 billion ns = 10 seconds
    pdu.set_avtp_timestamp(large_ts);
    EXPECT_EQ(pdu.get_avtp_timestamp(), large_ts);

    // Max value
    pdu.set_avtp_timestamp(0xFFFF'FFFF'FFFF'FFFFULL);
    EXPECT_EQ(pdu.get_avtp_timestamp(), 0xFFFF'FFFF'FFFF'FFFFULL);
}

TEST(aaf_v1, ptp_grandmaster_identity)
{
    AafV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32);

    pdu.set_tv(true);
    ClockIdentity gm(0xAABBCCDDEEFF0011ULL);
    pdu.set_ptp_grandmaster_identity(gm);
    EXPECT_EQ(pdu.get_ptp_grandmaster_identity(), gm);
}

TEST(aaf_v1, flags)
{
    AafV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32);

    pdu.set_mr(true);
    EXPECT_TRUE(pdu.mr());

    pdu.set_tv(true);
    EXPECT_TRUE(pdu.tv());

    pdu.set_tu(true);
    EXPECT_TRUE(pdu.tu());

    pdu.set_sp(true);
    EXPECT_TRUE(pdu.sp());

    pdu.set_evt(0x0A);
    EXPECT_EQ(pdu.evt(), 0x0AU);

    // sv and version preserved
    EXPECT_TRUE(pdu.sv());
    EXPECT_EQ(pdu.version(), 1);
}

TEST(aaf_v1, channels_per_frame)
{
    AafV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, AafFormat::int_24bit, AafSampleRate::rate_96_khz, 1, 24);

    pdu.set_channels_per_frame(1023);
    EXPECT_EQ(pdu.channels_per_frame(), 1023);

    pdu.set_channels_per_frame(8);
    EXPECT_EQ(pdu.channels_per_frame(), 8);

    // Verify nsr preserved when channels change
    EXPECT_EQ(pdu.nsr(), AafSampleRate::rate_96_khz);
}

TEST(aaf_v1, dimensions_and_sample_count)
{
    AafV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32);

    pdu.set_dimensions(6, 2);
    EXPECT_EQ(pdu.channels_per_frame(), 2);
    // 6 samples * 2 channels * 4 bytes = 48
    EXPECT_EQ(pdu.get_stream_data_length(), 48);
    EXPECT_EQ(pdu.sample_count(), 6);
}

TEST(aaf_v1, round_trip_serialization)
{
    AafV1Pdu original{};
    StreamId sid(Eui48(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF), 0x1234);
    original.init(sid, AafFormat::int_24bit, AafSampleRate::rate_48_khz, 8, 24);
    original.set_tv(true);
    original.set_sequence_num(0x12345678);
    original.set_avtp_timestamp(0x0000'0001'0000'0000ULL);
    ClockIdentity gm(0x0011223344556677ULL);
    original.set_ptp_grandmaster_identity(gm);
    original.set_dimensions(12, 8);

    std::array<uint8_t, 64> buf{};
    (void)store_unchecked(std::span<uint8_t>(buf), original);

    // Verify subtype and version bytes
    EXPECT_EQ(buf[0], AvtpSubtype::aaf);
    EXPECT_EQ((buf[1] >> 4) & 0x07, 1);  // version = 1

    // Deserialize
    AafV1Pdu loaded{};
    (void)load_unchecked(std::span<uint8_t const>(buf), &loaded);
    EXPECT_TRUE(loaded.is_valid());
    EXPECT_EQ(loaded.version(), 1);
    EXPECT_EQ(loaded.stream_id(), sid);
    EXPECT_EQ(loaded.get_sequence_num(), 0x12345678U);
    EXPECT_EQ(loaded.get_avtp_timestamp(), 0x0000'0001'0000'0000ULL);
    EXPECT_EQ(loaded.get_ptp_grandmaster_identity(), gm);
    EXPECT_EQ(loaded.get_format(), AafFormat::int_24bit);
    EXPECT_EQ(loaded.nsr(), AafSampleRate::rate_48_khz);
    EXPECT_EQ(loaded.channels_per_frame(), 8);
    EXPECT_EQ(loaded.get_bit_depth(), 24);
    EXPECT_EQ(loaded.sample_count(), 12);
}

TEST(aaf_v1, parse_valid)
{
    AafV1Pdu pdu{};
    StreamId sid(Eui48(0x11, 0x22, 0x33, 0x44, 0x55, 0x66), 0x0001);
    pdu.init(sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32);
    pdu.set_dimensions(6, 2);

    std::array<uint8_t, 96> buf{};
    span_store(buf, pdu);

    auto parsed = aaf_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->version(), 1);
    EXPECT_EQ(parsed->stream_id(), sid);
    EXPECT_EQ(parsed->channels_per_frame(), 2);
}

TEST(aaf_v1, parse_truncated)
{
    std::array<uint8_t, 32> buf{};
    buf[0] = AvtpSubtype::aaf;
    buf[1] = 0x90U;  // sv=1, version=1
    auto parsed = aaf_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_FALSE(parsed.has_value());
}

TEST(aaf_v1, parse_wrong_version)
{
    AafV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32);
    // Corrupt version to 0
    pdu.sv_version_flags = 0x80U;  // sv=1, version=0

    std::array<uint8_t, 40> buf{};
    span_store(buf, pdu);
    auto parsed = aaf_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_FALSE(parsed.has_value());
}

TEST(aaf_v1, parse_wrong_subtype)
{
    AafV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32);
    pdu.subtype = AvtpSubtype::crf;  // wrong

    std::array<uint8_t, 40> buf{};
    span_store(buf, pdu);
    auto parsed = aaf_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_FALSE(parsed.has_value());
}

TEST(aaf_v1, get_audio_payload)
{
    std::array<uint8_t, 64> buf{};
    buf[0] = AvtpSubtype::aaf;
    buf[40] = 0xBE;  // first byte of audio payload

    auto payload = aaf_v1_get_audio_payload(std::span<uint8_t const>(buf));
    EXPECT_EQ(payload.size(), 24U);
    EXPECT_EQ(payload[0], 0xBEU);

    auto empty = aaf_v1_get_audio_payload(std::span<uint8_t const>(buf.data(), AafV1Pdu::HEADER_LENGTH));
    EXPECT_TRUE(empty.empty());
}

TEST(aaf_v1, format_to_output)
{
    AafV1Pdu pdu{};
    StreamId sid(Eui48(0x00, 0x11, 0x22, 0x33, 0x44, 0x55), 0x0001);
    pdu.init(sid, AafFormat::float_32bit, AafSampleRate::rate_48_khz, 2, 32);
    pdu.set_tv(true);
    pdu.set_avtp_timestamp(1000);

    std::string result;
    format_to(std::back_inserter(result), pdu);
    EXPECT_TRUE(result.find("AAF(v1)") != std::string::npos);
    EXPECT_TRUE(result.find("32-bit float") != std::string::npos);
    EXPECT_TRUE(result.find("48 kHz") != std::string::npos);
}

TEST_MAIN(statusbar_avtp, avtp_aaf_v1_test)
