// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_escf.hpp"

#include "statusbar/avtp/avtp_escf_format.hpp"
#include "statusbar/buffer/span_utils.hpp"
#include "statusbar/test/test.hpp"

#include <array>

using namespace statusbar;
using namespace statusbar::avtp;
using namespace statusbar::ieee;

// Compile-time layout check
static_assert(sizeof(EscfPdu) == EscfPdu::HEADER_LENGTH);

TEST(escf, init_and_accessors)
{
    EscfPdu pdu{};
    Eui64 kid(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11);
    pdu.init(0, kid);

    EXPECT_EQ(pdu.subtype.get(), AvtpSubtype::escf);
    EXPECT_EQ(pdu.version(), 0);
    EXPECT_EQ(pdu.sig(), 0);
    EXPECT_EQ(pdu.control_data_length(), 0);
    EXPECT_EQ(pdu.key_id(), kid);
    EXPECT_TRUE(pdu.is_valid());
}

TEST(escf, control_data_length)
{
    EscfPdu pdu{};
    Eui64 kid{};
    pdu.init(0, kid);

    pdu.set_control_data_length(1000);
    EXPECT_EQ(pdu.control_data_length(), 1000);

    pdu.set_control_data_length(2047);
    EXPECT_EQ(pdu.control_data_length(), 2047);

    pdu.set_control_data_length(0xFFFF);
    EXPECT_EQ(pdu.control_data_length(), 2047);
}

TEST(escf, parse_valid)
{
    EscfPdu pdu{};
    Eui64 kid(0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88);
    pdu.init(0, kid);
    pdu.set_control_data_length(300);

    std::array<uint8_t, 32> buf{};
    span_store(buf, pdu);

    auto parsed = escf_parse_header(std::span<uint8_t const>(buf));
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->sig(), 0);
    EXPECT_EQ(parsed->key_id(), kid);
    EXPECT_EQ(parsed->control_data_length(), 300);
}

TEST(escf, parse_truncated)
{
    std::array<uint8_t, 8> buf{};
    buf[0] = AvtpSubtype::escf;
    auto parsed = escf_parse_header(std::span<uint8_t const>(buf));
    EXPECT_FALSE(parsed.has_value());
}

TEST(escf, parse_wrong_subtype)
{
    EscfPdu pdu{};
    Eui64 kid{};
    pdu.init(0, kid);
    pdu.subtype = AvtpSubtype::eecf;  // wrong

    std::array<uint8_t, 12> buf{};
    span_store(buf, pdu);
    auto parsed = escf_parse_header(std::span<uint8_t const>(buf));
    EXPECT_FALSE(parsed.has_value());
}

TEST(escf, get_signed_payload)
{
    std::array<uint8_t, 24> buf{};
    buf[0] = AvtpSubtype::escf;

    auto payload = escf_get_signed_payload(std::span<uint8_t const>(buf));
    EXPECT_EQ(payload.size(), 12U);

    auto empty = escf_get_signed_payload(std::span<uint8_t const>(buf.data(), EscfPdu::HEADER_LENGTH));
    EXPECT_TRUE(empty.empty());
}

TEST(escf, sig_mode_names)
{
    EXPECT_EQ(std::string(escf_sig_mode_name(0)), "ECC1");
    EXPECT_EQ(std::string(escf_sig_mode_name(1)), "Reserved");
}

TEST(escf, format_to_output)
{
    EscfPdu pdu{};
    Eui64 kid(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77);
    pdu.init(0, kid);
    pdu.set_control_data_length(64);

    std::string result;
    format_to(std::back_inserter(result), pdu);
    EXPECT_TRUE(result.find("ESCF") != std::string::npos);
    EXPECT_TRUE(result.find("ECC1") != std::string::npos);
    EXPECT_TRUE(result.find("64") != std::string::npos);
}

TEST_MAIN(statusbar_avtp, avtp_escf_test)
