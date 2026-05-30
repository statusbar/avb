// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_ntscf_v1.hpp"

#include "statusbar/avtp/avtp_ntscf_v1_format.hpp"
#include "statusbar/buffer/span_utils.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <string>

using namespace statusbar;
using namespace statusbar::avtp;
using namespace statusbar::ieee;
using namespace statusbar::tsn;

// Compile-time layout check
static_assert(sizeof(NtscfV1Pdu) == NtscfV1Pdu::HEADER_LENGTH);
static_assert(sizeof(NtscfV1Pdu) == 28);

TEST(ntscf_v1, init_and_accessors)
{
    NtscfV1Pdu pdu{};
    StreamId sid(Eui48(0x00, 0x11, 0x22, 0x33, 0x44, 0x55), 0x0001);
    pdu.init(sid);

    EXPECT_EQ(pdu.subtype.get(), AvtpSubtype::ntscf);
    EXPECT_TRUE(pdu.sv());
    EXPECT_EQ(pdu.version(), 1);
    EXPECT_EQ(pdu.get_sequence_num(), 0U);
    EXPECT_EQ(pdu.get_sequence_num_lsb(), 0U);
    EXPECT_EQ(pdu.stream_id(), sid);
    EXPECT_EQ(pdu.ntscf_data_length(), 0U);
    EXPECT_TRUE(pdu.is_valid());
}

TEST(ntscf_v1, init_sv_false)
{
    NtscfV1Pdu pdu{};
    StreamId sid(Eui48(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF), 0x0001);
    pdu.init(sid, false);

    EXPECT_FALSE(pdu.sv());
    EXPECT_EQ(pdu.version(), 1);
    EXPECT_EQ(pdu.stream_id(), sid);
    // sv=0 but version=1: still valid per is_valid (only checks subtype + version)
    EXPECT_TRUE(pdu.is_valid());
}

TEST(ntscf_v1, version_is_one)
{
    NtscfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid);

    EXPECT_EQ(pdu.version(), 1);
    // sv=1(0x80) | version=1(0x10) = 0x90
    EXPECT_EQ(pdu.sv_version_rsv.get(), 0x90U);
}

TEST(ntscf_v1, sequence_num_32bit)
{
    NtscfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid);

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

TEST(ntscf_v1, ptp_grandmaster_identity)
{
    NtscfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid);

    ClockIdentity gm(0xAABBCCDDEEFF0011ULL);
    pdu.set_ptp_grandmaster_identity(gm);
    EXPECT_EQ(pdu.get_ptp_grandmaster_identity(), gm);
}

TEST(ntscf_v1, data_length_small)
{
    NtscfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid);

    pdu.set_ntscf_data_length(100);
    EXPECT_EQ(pdu.ntscf_data_length(), 100U);
}

TEST(ntscf_v1, data_length_max)
{
    NtscfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid);

    pdu.set_ntscf_data_length(2047);
    EXPECT_EQ(pdu.ntscf_data_length(), 2047U);
}

TEST(ntscf_v1, data_length_preserves_other_bits)
{
    NtscfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid);

    // Set some data length, then set another — upper nibble bits should be preserved
    pdu.rsv_r_len_hi = 0xF0U;  // set reserved bits high
    pdu.set_ntscf_data_length(0x456);
    // Upper nibble (0xF0) preserved, low 3 bits = 0x04
    EXPECT_EQ(pdu.rsv_r_len_hi.get(), 0xF4U);
    EXPECT_EQ(pdu.ntscf_data_length(), 0x456U);
}

TEST(ntscf_v1, round_trip_serialization)
{
    NtscfV1Pdu original{};
    StreamId sid(Eui48(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF), 0x1234);
    original.init(sid);
    original.set_sequence_num(0x12345678);
    ClockIdentity gm(0x0011223344556677ULL);
    original.set_ptp_grandmaster_identity(gm);
    original.set_ntscf_data_length(512);

    std::array<uint8_t, 48> buf{};
    (void)store_unchecked(std::span<uint8_t>(buf), original);

    // Verify subtype and version
    EXPECT_EQ(buf[0], AvtpSubtype::ntscf);

    NtscfV1Pdu loaded{};
    (void)load_unchecked(std::span<uint8_t const>(buf), &loaded);
    EXPECT_TRUE(loaded.is_valid());
    EXPECT_EQ(loaded.version(), 1);
    EXPECT_EQ(loaded.stream_id(), sid);
    EXPECT_EQ(loaded.get_sequence_num(), 0x12345678U);
    EXPECT_EQ(loaded.get_ptp_grandmaster_identity(), gm);
    EXPECT_EQ(loaded.ntscf_data_length(), 512U);
}

TEST(ntscf_v1, parse_valid)
{
    NtscfV1Pdu pdu{};
    StreamId sid(Eui48(0x11, 0x22, 0x33, 0x44, 0x55, 0x66), 0x0001);
    pdu.init(sid);
    pdu.set_ntscf_data_length(64);

    std::array<uint8_t, 64> buf{};
    span_store(buf, pdu);

    auto parsed = ntscf_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->version(), 1);
    EXPECT_EQ(parsed->stream_id(), sid);
    EXPECT_EQ(parsed->ntscf_data_length(), 64U);
}

TEST(ntscf_v1, parse_truncated)
{
    std::array<uint8_t, 24> buf{};
    buf[0] = AvtpSubtype::ntscf;
    buf[1] = 0x90U;
    auto parsed = ntscf_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_FALSE(parsed.has_value());
}

TEST(ntscf_v1, parse_wrong_version)
{
    NtscfV1Pdu pdu{};
    StreamId sid{};
    pdu.init(sid);
    pdu.sv_version_rsv = 0x80U;  // sv=1, version=0

    std::array<uint8_t, 28> buf{};
    span_store(buf, pdu);
    auto parsed = ntscf_v1_parse_header(std::span<uint8_t const>(buf));
    EXPECT_FALSE(parsed.has_value());
}

TEST(ntscf_v1, get_acf_payload)
{
    std::array<uint8_t, 48> buf{};
    buf[0] = AvtpSubtype::ntscf;
    buf[28] = 0xCA;  // first byte of acf_payload_data

    auto data = ntscf_v1_get_acf_payload(std::span<uint8_t const>(buf));
    EXPECT_EQ(data.size(), 20U);
    EXPECT_EQ(data[0], 0xCAU);

    auto empty = ntscf_v1_get_acf_payload(std::span<uint8_t const>(buf.data(), NtscfV1Pdu::HEADER_LENGTH));
    EXPECT_TRUE(empty.empty());
}

TEST(ntscf_v1, format_to_output)
{
    NtscfV1Pdu pdu{};
    StreamId sid(Eui48(0x00, 0x11, 0x22, 0x33, 0x44, 0x55), 0x0001);
    pdu.init(sid);
    pdu.set_ntscf_data_length(32);

    std::string result;
    format_to(std::back_inserter(result), pdu);
    EXPECT_TRUE(result.find("NTSCF(v1)") != std::string::npos);
    EXPECT_TRUE(result.find("seq=0") != std::string::npos);
    EXPECT_TRUE(result.find("ntscf_data_length=32") != std::string::npos);
}

TEST_MAIN(statusbar_avtp, avtp_ntscf_v1_test)
