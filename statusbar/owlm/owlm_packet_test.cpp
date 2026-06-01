// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/owlm/owlm_packet.hpp"

#include "statusbar/test/test.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace {

using namespace statusbar;
using namespace statusbar::owlm;

auto make_sample_packet() -> OwlmPacket
{
    OwlmPacket p{};
    p.sender_eui64 = ieee::Eui64{0xAA, 0xBB, 0xCC, 0xFF, 0xFE, 0xDD, 0xEE, 0xFF};
    p.sequence = 0x12345678U;
    p.tx_gptp_ns = 0x0011223344556677LL;
    p.tx_interval_us = 1000U;
    return p;
}

TEST(owlm_packet, round_trip)
{
    OwlmPacket const original = make_sample_packet();
    std::array<uint8_t, OwlmPacket::HEADER_SIZE> buf{};
    auto written = encode_owlm_packet(original, buf);
    EXPECT_EQ(written, OwlmPacket::HEADER_SIZE);

    OwlmPacket decoded{};
    auto ec = decode_owlm_packet(std::span<uint8_t const>(buf), decoded);
    EXPECT_FALSE(static_cast<bool>(ec));
    EXPECT_EQ(decoded.sequence, original.sequence);
    EXPECT_EQ(decoded.tx_gptp_ns, original.tx_gptp_ns);
    EXPECT_EQ(decoded.tx_interval_us, original.tx_interval_us);
    auto const a = decoded.sender_eui64.span();
    auto const b = original.sender_eui64.span();
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(a[i], b[i]);
    }
}

TEST(owlm_packet, magic_layout)
{
    OwlmPacket const p = make_sample_packet();
    std::array<uint8_t, OwlmPacket::HEADER_SIZE> buf{};
    encode_owlm_packet(p, buf);
    EXPECT_EQ(buf[0], uint8_t{'O'});
    EXPECT_EQ(buf[1], uint8_t{'W'});
    EXPECT_EQ(buf[2], uint8_t{'L'});
    EXPECT_EQ(buf[3], uint8_t{'M'});
    EXPECT_EQ(buf[4], uint8_t{0});  // version high
    EXPECT_EQ(buf[5], uint8_t{1});  // version low
    EXPECT_EQ(buf[6], uint8_t{0});  // flags high
    EXPECT_EQ(buf[7], uint8_t{0});  // flags low
}

TEST(owlm_packet, reject_bad_magic)
{
    std::array<uint8_t, OwlmPacket::HEADER_SIZE> buf{};
    OwlmPacket const p = make_sample_packet();
    encode_owlm_packet(p, buf);
    buf[0] = 'X';
    OwlmPacket decoded{};
    auto ec = decode_owlm_packet(buf, decoded);
    EXPECT_TRUE(static_cast<bool>(ec));
    EXPECT_EQ(ec, make_error_code(OwlmError::InvalidMagic));
}

TEST(owlm_packet, reject_bad_version)
{
    std::array<uint8_t, OwlmPacket::HEADER_SIZE> buf{};
    OwlmPacket const p = make_sample_packet();
    encode_owlm_packet(p, buf);
    buf[5] = 99;
    OwlmPacket decoded{};
    auto ec = decode_owlm_packet(buf, decoded);
    EXPECT_EQ(ec, make_error_code(OwlmError::UnsupportedVersion));
}

TEST(owlm_packet, reject_reserved_flags)
{
    std::array<uint8_t, OwlmPacket::HEADER_SIZE> buf{};
    OwlmPacket const p = make_sample_packet();
    encode_owlm_packet(p, buf);
    buf[7] = 0x01;
    OwlmPacket decoded{};
    auto ec = decode_owlm_packet(buf, decoded);
    EXPECT_EQ(ec, make_error_code(OwlmError::ReservedFlagsSet));
}

TEST(owlm_packet, reject_too_short)
{
    std::array<uint8_t, OwlmPacket::HEADER_SIZE - 1> buf{};
    OwlmPacket decoded{};
    auto ec = decode_owlm_packet(buf, decoded);
    EXPECT_EQ(ec, make_error_code(OwlmError::DatagramTooShort));
}

TEST(owlm_packet, reject_invalid_interval_zero)
{
    OwlmPacket p = make_sample_packet();
    p.tx_interval_us = 0;
    std::array<uint8_t, OwlmPacket::HEADER_SIZE> buf{};
    encode_owlm_packet(p, buf);
    OwlmPacket decoded{};
    auto ec = decode_owlm_packet(buf, decoded);
    EXPECT_EQ(ec, make_error_code(OwlmError::InvalidInterval));
}

TEST(owlm_packet, reject_invalid_interval_too_large)
{
    OwlmPacket p = make_sample_packet();
    p.tx_interval_us = 60'000'001U;
    std::array<uint8_t, OwlmPacket::HEADER_SIZE> buf{};
    encode_owlm_packet(p, buf);
    OwlmPacket decoded{};
    auto ec = decode_owlm_packet(buf, decoded);
    EXPECT_EQ(ec, make_error_code(OwlmError::InvalidInterval));
}

TEST(owlm_packet, reject_sender_not_synced)
{
    OwlmPacket p = make_sample_packet();
    p.tx_gptp_ns = 0;
    std::array<uint8_t, OwlmPacket::HEADER_SIZE> buf{};
    encode_owlm_packet(p, buf);
    OwlmPacket decoded{};
    auto ec = decode_owlm_packet(buf, decoded);
    EXPECT_EQ(ec, make_error_code(OwlmError::SenderNotSynced));
}

TEST(owlm_packet, accepts_buf_larger_than_header)
{
    std::array<uint8_t, OwlmPacket::HEADER_SIZE + 64> buf{};
    OwlmPacket const p = make_sample_packet();
    encode_owlm_packet(p, std::span<uint8_t>(buf.data(), OwlmPacket::HEADER_SIZE));
    OwlmPacket decoded{};
    auto ec = decode_owlm_packet(buf, decoded);
    EXPECT_FALSE(static_cast<bool>(ec));
    EXPECT_EQ(decoded.sequence, p.sequence);
}

}  // namespace

TEST_MAIN(statusbar_owlm, owlm_packet_test)
