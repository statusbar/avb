// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_tscf_v1.hpp"

#include "statusbar/avtp/avtp_tscf_v1_format.hpp"
#include "statusbar/buffer/span_utils.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <string>

using namespace statusbar;
using namespace statusbar::avtp;
using namespace statusbar::ieee;
using namespace statusbar::tsn;

// Compile-time layout check
static_assert(sizeof(TscfV1Pdu) == TscfV1Pdu::HEADER_LENGTH);
static_assert(sizeof(TscfV1Pdu) == 40);

TEST(tscf_v1, init_and_accessors)
{
    TscfV1Pdu pdu{};
    StreamId sid(Eui48(0x00, 0x11, 0x22, 0x33, 0x44, 0x55), 0x0001);
    pdu.init(sid);

    EXPECT_EQ(pdu.subtype.get(), AvtpSubtype::tscf);
    EXPECT_TRUE(pdu.sv());
    EXPECT_EQ(pdu.version(), 1);
    EXPECT_FALSE(pdu.mr());
    EXPECT_FALSE(pdu.tv());
    EXPECT_FALSE(pdu.tu());
    EXPECT_EQ(pdu.get_sequence_num(), 0U);
    EXPECT_EQ(pdu.stream_id(), sid);
    EXPECT_EQ(pdu.get_avtp_timestamp(), 0U);
    EXPECT_EQ(pdu.get_stream_data_length(), 0);
    EXPECT_TRUE(pdu.is_valid());
}

TEST(tscf_v1, init_sv_false)
{
    TscfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid, false);

    EXPECT_FALSE(pdu.sv());
    EXPECT_EQ(pdu.version(), 1);
    EXPECT_TRUE(pdu.is_valid());
}

TEST(tscf_v1, version_is_one)
{
    TscfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid);

    EXPECT_EQ(pdu.version(), 1);
}

TEST(tscf_v1, sequence_num_32bit)
{
    TscfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid);

    pdu.set_sequence_num(100000);
    EXPECT_EQ(pdu.get_sequence_num(), 100000U);
    // sequence_num_lsb mirrors low 8 bits
    EXPECT_EQ(pdu.sequence_num_lsb.get(), static_cast<uint8_t>(100000U & 0xFFU));

    // Wrap test
    pdu.set_sequence_num(0xFFFFFFFF);
    pdu.increment_sequence_num();
    EXPECT_EQ(pdu.get_sequence_num(), 0U);
    EXPECT_EQ(pdu.sequence_num_lsb.get(), 0U);
}

TEST(tscf_v1, timestamp_64bit)
{
    TscfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid);

    pdu.set_tv(true);
    uint64_t const large_ts = 0x0000'0002'540B'E400ULL;  // 10 seconds in ns
    pdu.set_avtp_timestamp(large_ts);
    EXPECT_EQ(pdu.get_avtp_timestamp(), large_ts);
}

TEST(tscf_v1, ptp_grandmaster_identity)
{
    TscfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid);

    pdu.set_tv(true);
    ClockIdentity gm(0xAABBCCDDEEFF0011ULL);
    pdu.set_ptp_grandmaster_identity(gm);
    EXPECT_EQ(pdu.get_ptp_grandmaster_identity(), gm);
}

TEST(tscf_v1, flags)
{
    TscfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid);

    pdu.set_mr(true);
    EXPECT_TRUE(pdu.mr());

    pdu.set_tv(true);
    EXPECT_TRUE(pdu.tv());

    pdu.set_tu(true);
    EXPECT_TRUE(pdu.tu());

    // sv and version preserved
    EXPECT_TRUE(pdu.sv());
    EXPECT_EQ(pdu.version(), 1);
}

TEST(tscf_v1, stream_data_length)
{
    TscfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid);

    pdu.set_stream_data_length(500);
    EXPECT_EQ(pdu.get_stream_data_length(), 500);

    pdu.set_stream_data_length(0xFFFF);
    EXPECT_EQ(pdu.get_stream_data_length(), 0xFFFF);
}

TEST(tscf_v1, round_trip_serialization)
{
    TscfV1Pdu original{};
    StreamId sid(Eui48(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF), 0x1234);
    original.init(sid);
    original.set_tv(true);
    original.set_sequence_num(0xDEADBEEF);
    original.set_avtp_timestamp(0x0000'0001'0000'0000ULL);
    ClockIdentity gm(0x0011223344556677ULL);
    original.set_ptp_grandmaster_identity(gm);
    original.set_stream_data_length(200);

    std::array<uint8_t, 48> buf{};
    (void)store_unchecked(std::span<uint8_t>(buf), original);

    // Verify subtype byte
    EXPECT_EQ(buf[0], AvtpSubtype::tscf);

    TscfV1Pdu loaded{};
    (void)load_unchecked(std::span<uint8_t const>(buf), &loaded);
    EXPECT_TRUE(loaded.is_valid());
    EXPECT_EQ(loaded.version(), 1);
    EXPECT_TRUE(loaded.sv());
    EXPECT_TRUE(loaded.tv());
    EXPECT_EQ(loaded.stream_id(), sid);
    EXPECT_EQ(loaded.get_sequence_num(), 0xDEADBEEFU);
    EXPECT_EQ(loaded.get_avtp_timestamp(), 0x0000'0001'0000'0000ULL);
    EXPECT_EQ(loaded.get_ptp_grandmaster_identity(), gm);
    EXPECT_EQ(loaded.get_stream_data_length(), 200);
}

TEST(tscf_v1, parse_valid)
{
    TscfV1Pdu pdu{};
    StreamId sid(Eui48(0x11, 0x22, 0x33, 0x44, 0x55, 0x66), 0x0001);
    pdu.init(sid);
    pdu.set_stream_data_length(100);

    std::array<uint8_t, 64> buf{};
    span_store(buf, pdu);

    auto parsed = tscf_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->version(), 1);
    EXPECT_EQ(parsed->stream_id(), sid);
    EXPECT_EQ(parsed->get_stream_data_length(), 100);
}

TEST(tscf_v1, parse_truncated)
{
    std::array<uint8_t, 32> buf{};
    buf[0] = AvtpSubtype::tscf;
    buf[1] = 0x90U;  // sv=1, version=1
    auto parsed = tscf_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_FALSE(parsed.has_value());
}

TEST(tscf_v1, parse_wrong_version)
{
    TscfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid);
    pdu.sv_version_flags = 0x80U;  // sv=1, version=0

    std::array<uint8_t, 40> buf{};
    span_store(buf, pdu);
    auto parsed = tscf_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_FALSE(parsed.has_value());
}

TEST(tscf_v1, parse_wrong_subtype)
{
    TscfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid);
    pdu.subtype = AvtpSubtype::aaf;  // wrong

    std::array<uint8_t, 40> buf{};
    span_store(buf, pdu);
    auto parsed = tscf_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_FALSE(parsed.has_value());
}

TEST(tscf_v1, get_acf_payload)
{
    std::array<uint8_t, 56> buf{};
    buf[0] = AvtpSubtype::tscf;
    buf[40] = 0xBE;  // first byte of ACF payload

    auto payload = tscf_v1_get_acf_payload(std::span<uint8_t const>(buf));
    EXPECT_EQ(payload.size(), 16U);
    EXPECT_EQ(payload[0], 0xBEU);

    auto empty = tscf_v1_get_acf_payload(std::span<uint8_t const>(buf.data(), TscfV1Pdu::HEADER_LENGTH));
    EXPECT_TRUE(empty.empty());
}

TEST(tscf_v1, format_to_output)
{
    TscfV1Pdu pdu{};
    StreamId sid(Eui48(0x00, 0x11, 0x22, 0x33, 0x44, 0x55), 0x0001);
    pdu.init(sid);
    pdu.set_tv(true);
    pdu.set_avtp_timestamp(5000);
    pdu.set_stream_data_length(64);

    std::string result;
    format_to(std::back_inserter(result), pdu);
    EXPECT_TRUE(result.find("TSCF(v1)") != std::string::npos);
    EXPECT_TRUE(result.find("5000") != std::string::npos);
    EXPECT_TRUE(result.find("64") != std::string::npos);
}

TEST_MAIN(statusbar_avtp, avtp_tscf_v1_test)
