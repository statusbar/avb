// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_ip_encap.hpp"

#include "statusbar/avtp/avtp_ip_encap_format.hpp"
#include "statusbar/buffer/span_utils.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <string>

using namespace statusbar;
using namespace statusbar::avtp;

// Compile-time layout check
static_assert(sizeof(IpAvtpduHeader) == IpAvtpduHeader::LENGTH);

TEST(ip_encap, init_and_accessors)
{
    IpAvtpduHeader header{};
    header.init(42);

    EXPECT_EQ(header.encapsulation_sequence_num(), 42U);

    header.set_encapsulation_sequence_num(0xDEADBEEF);
    EXPECT_EQ(header.encapsulation_sequence_num(), 0xDEADBEEFU);
}

TEST(ip_encap, init_default_zero)
{
    IpAvtpduHeader header{};
    header.init();

    EXPECT_EQ(header.encapsulation_sequence_num(), 0U);
}

TEST(ip_encap, round_trip_serialization)
{
    IpAvtpduHeader original{};
    original.init(0x12345678);

    // Serialize
    std::array<uint8_t, 32> buf{};
    (void)store_unchecked(std::span<uint8_t>(buf), original);

    // Verify big-endian wire format
    EXPECT_EQ(buf[0], 0x12U);
    EXPECT_EQ(buf[1], 0x34U);
    EXPECT_EQ(buf[2], 0x56U);
    EXPECT_EQ(buf[3], 0x78U);

    // Deserialize
    IpAvtpduHeader loaded{};
    (void)load_unchecked(std::span<uint8_t const>(buf), &loaded);
    EXPECT_EQ(loaded.encapsulation_sequence_num(), 0x12345678U);
}

TEST(ip_encap, parse_header_valid)
{
    IpAvtpduHeader original{};
    original.init(999);

    std::array<uint8_t, 32> buf{};
    (void)store_unchecked(std::span<uint8_t>(buf), original);

    auto parsed = ip_avtpdu_parse_header(std::span<uint8_t const>(buf));
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->encapsulation_sequence_num(), 999U);
}

TEST(ip_encap, parse_header_too_small)
{
    std::array<uint8_t, 3> buf{};
    auto parsed = ip_avtpdu_parse_header(std::span<uint8_t const>(buf));
    EXPECT_FALSE(parsed.has_value());
}

TEST(ip_encap, parse_header_exact_size)
{
    std::array<uint8_t, 4> buf{0x00, 0x00, 0x00, 0x01};
    auto parsed = ip_avtpdu_parse_header(std::span<uint8_t const>(buf));
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->encapsulation_sequence_num(), 1U);
}

TEST(ip_encap, get_payload)
{
    // 4-byte header + 8-byte mock AVTPDU
    std::array<uint8_t, 12> buf{};
    buf[4] = 0xAA;  // first byte of encapsulated AVTPDU

    auto payload = ip_avtpdu_get_payload(std::span<uint8_t const>(buf));
    EXPECT_EQ(payload.size(), 8U);
    EXPECT_EQ(payload[0], 0xAAU);
}

TEST(ip_encap, get_payload_header_only)
{
    std::array<uint8_t, 4> buf{};
    auto payload = ip_avtpdu_get_payload(std::span<uint8_t const>(buf));
    EXPECT_TRUE(payload.empty());
}

TEST(ip_encap, get_payload_too_small)
{
    std::array<uint8_t, 2> buf{};
    auto payload = ip_avtpdu_get_payload(std::span<uint8_t const>(buf));
    EXPECT_TRUE(payload.empty());
}

//
// Port selection tests
//

TEST(ip_encap, port_continuous_subtypes)
{
    EXPECT_EQ(ip_avtpdu_destination_port(AvtpSubtype::aaf), 17220U);
    EXPECT_EQ(ip_avtpdu_destination_port(AvtpSubtype::iec_61883_iidc), 17220U);
    EXPECT_EQ(ip_avtpdu_destination_port(AvtpSubtype::cvf), 17220U);
    EXPECT_EQ(ip_avtpdu_destination_port(AvtpSubtype::crf), 17220U);
    EXPECT_EQ(ip_avtpdu_destination_port(AvtpSubtype::aef_continuous), 17220U);
}

TEST(ip_encap, port_discrete_subtypes)
{
    EXPECT_EQ(ip_avtpdu_destination_port(AvtpSubtype::ntscf), 17221U);
    EXPECT_EQ(ip_avtpdu_destination_port(AvtpSubtype::adp), 17221U);
    EXPECT_EQ(ip_avtpdu_destination_port(AvtpSubtype::aecp), 17221U);
    EXPECT_EQ(ip_avtpdu_destination_port(AvtpSubtype::acmp), 17221U);
    EXPECT_EQ(ip_avtpdu_destination_port(AvtpSubtype::maap), 17221U);
    EXPECT_EQ(ip_avtpdu_destination_port(AvtpSubtype::aef_discrete), 17221U);
}

//
// Sequencer tests
//

TEST(ip_encap, sequencer_default_start)
{
    IpAvtpduSequencer seq;
    EXPECT_EQ(seq.current(), 0U);
    EXPECT_EQ(seq.next(), 0U);
    EXPECT_EQ(seq.current(), 1U);
    EXPECT_EQ(seq.next(), 1U);
    EXPECT_EQ(seq.current(), 2U);
}

TEST(ip_encap, sequencer_custom_start)
{
    IpAvtpduSequencer seq(100);
    EXPECT_EQ(seq.next(), 100U);
    EXPECT_EQ(seq.next(), 101U);
}

TEST(ip_encap, sequencer_wrap)
{
    IpAvtpduSequencer seq(0xFFFFFFFE);
    EXPECT_EQ(seq.next(), 0xFFFFFFFEU);
    EXPECT_EQ(seq.next(), 0xFFFFFFFFU);
    EXPECT_EQ(seq.next(), 0x00000000U);
    EXPECT_EQ(seq.next(), 0x00000001U);
}

TEST(ip_encap, sequencer_reset)
{
    IpAvtpduSequencer seq(50);
    (void)seq.next();
    (void)seq.next();
    EXPECT_EQ(seq.current(), 52U);

    seq.reset(0);
    EXPECT_EQ(seq.current(), 0U);
}

//
// Formatting test
//

TEST(ip_encap, format_to_output)
{
    IpAvtpduHeader header{};
    header.init(12345);

    std::string result;
    format_to(std::back_inserter(result), header);
    EXPECT_TRUE(result.find("IP-AVTPDU") != std::string::npos);
    EXPECT_TRUE(result.find("12345") != std::string::npos);
}

TEST_MAIN(statusbar_avtp, avtp_ip_encap_test)
