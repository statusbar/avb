// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_crf_v1.hpp"

#include "statusbar/avtp/avtp_crf_v1_format.hpp"
#include "statusbar/buffer/span_utils.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <string>

using namespace statusbar;
using namespace statusbar::avtp;
using namespace statusbar::ieee;
using namespace statusbar::tsn;

namespace {
[[nodiscard]] constexpr auto approx_equal(double a, double b, double epsilon) noexcept -> bool
{
    return (a - b) < epsilon && (b - a) < epsilon;
}
}  // namespace

// Compile-time layout check
static_assert(sizeof(CrfV1Pdu) == CrfV1Pdu::HEADER_LENGTH);
static_assert(sizeof(CrfV1Pdu) == 36);

TEST(crf_v1, init_audio_sample)
{
    CrfV1Pdu pdu{};
    StreamId sid(Eui48(0x00, 0x11, 0x22, 0x33, 0x44, 0x55), 0x0001);
    pdu.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160);

    EXPECT_EQ(pdu.subtype.get(), AvtpSubtype::crf);
    EXPECT_TRUE(pdu.sv());
    EXPECT_EQ(pdu.version(), 1);
    EXPECT_FALSE(pdu.mr());
    EXPECT_FALSE(pdu.fs());
    EXPECT_FALSE(pdu.tu());
    EXPECT_EQ(pdu.get_sequence_num(), 0U);
    EXPECT_EQ(pdu.stream_id(), sid);
    EXPECT_EQ(pdu.get_type(), CrfType::audio_sample);
    EXPECT_EQ(pdu.pull(), 0);
    EXPECT_EQ(pdu.base_frequency(), 48000U);
    EXPECT_EQ(pdu.crf_data_length(), 0);
    EXPECT_EQ(pdu.timestamp_interval(), 160);
    EXPECT_TRUE(pdu.is_valid());
}

TEST(crf_v1, init_generic)
{
    CrfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, CrfType::video_frame, 30, CrfPull::multiply_1_div_1001, 1);

    EXPECT_EQ(pdu.get_type(), CrfType::video_frame);
    EXPECT_EQ(pdu.base_frequency(), 30U);
    EXPECT_EQ(pdu.pull(), static_cast<uint8_t>(CrfPull::multiply_1_div_1001));
    EXPECT_EQ(pdu.timestamp_interval(), 1);
    EXPECT_TRUE(pdu.is_valid());
}

TEST(crf_v1, version_is_one)
{
    CrfV1Pdu pdu{};
    StreamId sid{};
    pdu.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160);

    EXPECT_EQ(pdu.version(), 1);
    // sv=1(0x80) | version=1(0x10) = 0x90
    EXPECT_EQ(pdu.sv_version_rsv.get(), 0x90U);
}

TEST(crf_v1, sequence_num_32bit)
{
    CrfV1Pdu pdu{};
    StreamId sid{};
    pdu.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160);

    pdu.set_sequence_num(100000);
    EXPECT_EQ(pdu.get_sequence_num(), 100000U);
    // sequence_num_lsb should mirror low 8 bits
    EXPECT_EQ(pdu.sequence_num_lsb.get(), static_cast<uint8_t>(100000U & 0xFFU));

    // Wrap test
    pdu.set_sequence_num(0xFFFFFFFF);
    pdu.increment_sequence_num();
    EXPECT_EQ(pdu.get_sequence_num(), 0U);
    EXPECT_EQ(pdu.sequence_num_lsb.get(), 0U);
}

TEST(crf_v1, ptp_grandmaster_identity)
{
    CrfV1Pdu pdu{};
    StreamId sid{};
    pdu.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160);

    ClockIdentity gm(0xAABBCCDDEEFF0011ULL);
    pdu.set_ptp_grandmaster_identity(gm);
    EXPECT_EQ(pdu.get_ptp_grandmaster_identity(), gm);
}

TEST(crf_v1, flags)
{
    CrfV1Pdu pdu{};
    StreamId sid{};
    pdu.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160);

    pdu.set_mr(true);
    EXPECT_TRUE(pdu.mr());

    pdu.set_fs(true);
    EXPECT_TRUE(pdu.fs());

    pdu.set_tu(true);
    EXPECT_TRUE(pdu.tu());

    // sv and version preserved
    EXPECT_TRUE(pdu.sv());
    EXPECT_EQ(pdu.version(), 1);
}

TEST(crf_v1, pull_and_base_frequency)
{
    CrfV1Pdu pdu{};
    StreamId sid{};
    pdu.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160);

    // Change pull, verify base_frequency preserved
    pdu.set_pull(static_cast<uint8_t>(CrfPull::multiply_1001));
    EXPECT_EQ(pdu.pull(), static_cast<uint8_t>(CrfPull::multiply_1001));
    EXPECT_EQ(pdu.base_frequency(), 48000U);

    // Change base_frequency, verify pull preserved
    pdu.set_base_frequency(96000);
    EXPECT_EQ(pdu.base_frequency(), 96000U);
    EXPECT_EQ(pdu.pull(), static_cast<uint8_t>(CrfPull::multiply_1001));

    // Max base_frequency (29 bits)
    pdu.set_base_frequency(0x1FFFFFFFU);
    EXPECT_EQ(pdu.base_frequency(), 0x1FFFFFFFU);
}

TEST(crf_v1, crf_data_length_and_timestamp_count)
{
    CrfV1Pdu pdu{};
    StreamId sid{};
    pdu.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160);

    pdu.set_crf_data_length(48);  // 6 timestamps * 8 bytes
    EXPECT_EQ(pdu.crf_data_length(), 48);
    EXPECT_EQ(pdu.timestamp_count(), 6);
}

TEST(crf_v1, actual_frequency)
{
    CrfV1Pdu pdu{};
    StreamId sid{};
    pdu.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160);

    EXPECT_TRUE(approx_equal(pdu.actual_frequency(), 48000.0, 0.001));

    pdu.set_pull(static_cast<uint8_t>(CrfPull::multiply_1_div_1001));
    EXPECT_TRUE(approx_equal(pdu.actual_frequency(), 48000.0 / 1.001, 0.1));
}

TEST(crf_v1, round_trip_serialization)
{
    CrfV1Pdu original{};
    StreamId sid(Eui48(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF), 0x1234);
    original.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160);
    original.set_sequence_num(0x12345678);
    ClockIdentity gm(0x0011223344556677ULL);
    original.set_ptp_grandmaster_identity(gm);
    original.set_crf_data_length(48);

    std::array<uint8_t, 48> buf{};
    (void)store_unchecked(std::span<uint8_t>(buf), original);

    // Verify subtype and version
    EXPECT_EQ(buf[0], AvtpSubtype::crf);

    CrfV1Pdu loaded{};
    (void)load_unchecked(std::span<uint8_t const>(buf), &loaded);
    EXPECT_TRUE(loaded.is_valid());
    EXPECT_EQ(loaded.version(), 1);
    EXPECT_EQ(loaded.stream_id(), sid);
    EXPECT_EQ(loaded.get_sequence_num(), 0x12345678U);
    EXPECT_EQ(loaded.get_ptp_grandmaster_identity(), gm);
    EXPECT_EQ(loaded.get_type(), CrfType::audio_sample);
    EXPECT_EQ(loaded.base_frequency(), 48000U);
    EXPECT_EQ(loaded.timestamp_interval(), 160);
    EXPECT_EQ(loaded.crf_data_length(), 48);
}

TEST(crf_v1, parse_valid)
{
    CrfV1Pdu pdu{};
    StreamId sid(Eui48(0x11, 0x22, 0x33, 0x44, 0x55, 0x66), 0x0001);
    pdu.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160);

    std::array<uint8_t, 64> buf{};
    span_store(buf, pdu);

    auto parsed = crf_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->version(), 1);
    EXPECT_EQ(parsed->stream_id(), sid);
    EXPECT_EQ(parsed->base_frequency(), 48000U);
}

TEST(crf_v1, parse_truncated)
{
    std::array<uint8_t, 32> buf{};
    buf[0] = AvtpSubtype::crf;
    buf[1] = 0x90U;
    auto parsed = crf_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_FALSE(parsed.has_value());
}

TEST(crf_v1, parse_wrong_version)
{
    CrfV1Pdu pdu{};
    StreamId sid{};
    pdu.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160);
    pdu.sv_version_rsv = 0x80U;  // sv=1, version=0

    std::array<uint8_t, 36> buf{};
    span_store(buf, pdu);
    auto parsed = crf_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_FALSE(parsed.has_value());
}

TEST(crf_v1, get_timestamp_data)
{
    std::array<uint8_t, 52> buf{};
    buf[0] = AvtpSubtype::crf;
    buf[36] = 0xCA;  // first byte of crf_data

    auto data = crf_v1_get_timestamp_data(std::span<uint8_t const>(buf));
    EXPECT_EQ(data.size(), 16U);
    EXPECT_EQ(data[0], 0xCAU);

    auto empty = crf_v1_get_timestamp_data(std::span<uint8_t const>(buf.data(), CrfV1Pdu::HEADER_LENGTH));
    EXPECT_TRUE(empty.empty());
}

TEST(crf_v1, format_to_output)
{
    CrfV1Pdu pdu{};
    StreamId sid(Eui48(0x00, 0x11, 0x22, 0x33, 0x44, 0x55), 0x0001);
    pdu.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160);

    std::string result;
    format_to(std::back_inserter(result), pdu);
    EXPECT_TRUE(result.find("CRF(v1)") != std::string::npos);
    EXPECT_TRUE(result.find("Audio Sample") != std::string::npos);
    EXPECT_TRUE(result.find("48000") != std::string::npos);
}

TEST_MAIN(statusbar_avtp, avtp_crf_v1_test)
