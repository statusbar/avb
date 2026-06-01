// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/owlm/owlm_codec.hpp"

#include "statusbar/owlm/owlm_eui64.hpp"
#include "statusbar/owlm/owlm_packet.hpp"
#include "statusbar/test/test.hpp"
#include "statusbar/udptun/udptun_codec_concept.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

using namespace statusbar;

namespace {

/// Construct an EUI-64 with explicit OUI (3 bytes) and NIC ID (3 bytes),
/// inserting `mid_bytes` in between. Mirrors `make_owlm_eui64` so we can
/// build primary, redundant, and legacy identities with the same shape.
auto make_eui64(uint16_t mid_bytes) -> ieee::Eui64
{
    ieee::Eui48 const mac{0xAA, 0x11, 0x22, 0x33, 0x44, 0x55};
    return owlm::make_owlm_eui64(mac, mid_bytes);
}

auto make_test_packet(ieee::Eui64 const& sender, uint32_t sequence = 7) -> std::vector<uint8_t>
{
    owlm::OwlmPacket p{};
    p.sender_eui64 = sender;
    p.sequence = sequence;
    p.tx_gptp_ns = 1'000'000;
    p.tx_interval_us = 100'000;

    std::vector<uint8_t> buf(owlm::OwlmPacket::HEADER_SIZE, 0);
    owlm::encode_owlm_packet(p, buf);
    return buf;
}

auto make_test_packet_legacy_oui(uint8_t eui_first_byte) -> std::vector<uint8_t>
{
    owlm::OwlmPacket p{};
    auto eui_span = p.sender_eui64.span();
    eui_span[0] = eui_first_byte;
    eui_span[1] = 0x11;
    eui_span[2] = 0x22;
    eui_span[3] = 0xFF;
    eui_span[4] = 0xFE;
    eui_span[5] = 0x33;
    eui_span[6] = 0x44;
    eui_span[7] = 0x55;
    p.sequence = 7;
    p.tx_gptp_ns = 1'000'000;
    p.tx_interval_us = 100'000;

    std::vector<uint8_t> buf(owlm::OwlmPacket::HEADER_SIZE, 0);
    owlm::encode_owlm_packet(p, buf);
    return buf;
}

}  // namespace

// ---------------------------------------------------------------------------
// validate_for_reflect — the original three tests, retained for regression.
// ---------------------------------------------------------------------------

TEST(owlm_codec_validate, accepts_valid_packet)
{
    owlm::OwlmCodec const codec{};
    auto buf = make_test_packet_legacy_oui(0xAA);
    EXPECT_TRUE(codec.validate_for_reflect(std::span<uint8_t const>(buf)));
}

TEST(owlm_codec_validate, rejects_short_buffer)
{
    owlm::OwlmCodec const codec{};
    std::array<uint8_t, 10> buf{};
    EXPECT_FALSE(codec.validate_for_reflect(std::span<uint8_t const>(buf)));
}

TEST(owlm_codec_validate, rejects_bad_magic)
{
    owlm::OwlmCodec const codec{};
    auto buf = make_test_packet_legacy_oui(0xAA);
    buf[0] = 0xFF;  // corrupt magic
    EXPECT_FALSE(codec.validate_for_reflect(std::span<uint8_t const>(buf)));
}

// ---------------------------------------------------------------------------
// Concept compliance — covered by the static_assert in owlm_codec.hpp, but
// re-asserted here so a regression in the concept is caught in this section.
// ---------------------------------------------------------------------------

TEST(owlm_codec_concept, satisfies_udptun_codec)
{
    static_assert(udptun::Codec<owlm::OwlmCodec>);
    EXPECT_EQ(owlm::OwlmCodec::header_size(), owlm::OwlmPacket::HEADER_SIZE);
}

// ---------------------------------------------------------------------------
// encode/decode round-trip
// ---------------------------------------------------------------------------

TEST(owlm_codec_roundtrip, encode_then_decode_recovers_fields)
{
    owlm::OwlmCodec const codec{};
    auto const sender = make_eui64(owlm::PRIMARY_MID_BYTES);
    std::vector<uint8_t> buf(owlm::OwlmPacket::HEADER_SIZE, 0);
    auto const written = codec.encode(buf, sender, /*sequence*/ 42, /*tx_gptp_ns*/ 1'234'567'890LL, /*interval_us*/ 250'000);
    EXPECT_EQ(written, owlm::OwlmPacket::HEADER_SIZE);

    auto pkt = codec.decode(std::span<uint8_t const>(buf));
    EXPECT_TRUE(pkt.has_value());
    EXPECT_EQ(codec.sender_id(*pkt), sender);
    EXPECT_EQ(codec.sequence(*pkt), uint32_t{42});
    EXPECT_EQ(codec.tx_gptp_ns(*pkt), int64_t{1'234'567'890LL});
    EXPECT_EQ(codec.announced_interval_us(*pkt), uint32_t{250'000});
}

TEST(owlm_codec_roundtrip, decode_returns_nullopt_on_short_buffer)
{
    owlm::OwlmCodec const codec{};
    std::array<uint8_t, 10> buf{};
    EXPECT_FALSE(codec.decode(std::span<uint8_t const>(buf)).has_value());
}

// ---------------------------------------------------------------------------
// classify — exhaustively covers the 6 PacketRole values.
// ---------------------------------------------------------------------------

TEST(owlm_codec_classify, self_primary)
{
    owlm::OwlmCodec const codec{};
    auto const sender = make_eui64(owlm::PRIMARY_MID_BYTES);
    auto const my_pair_id = owlm::eui64_pair_id(sender);
    auto const buf = make_test_packet(sender);
    auto const pkt = codec.decode(std::span<uint8_t const>(buf));
    EXPECT_TRUE(pkt.has_value());
    EXPECT_TRUE(codec.classify(*pkt, my_pair_id) == udptun::PacketRole::SelfPrimary);
}

TEST(owlm_codec_classify, self_redundant)
{
    owlm::OwlmCodec const codec{};
    auto const sender = make_eui64(owlm::REDUNDANT_MID_BYTES);
    auto const my_pair_id = owlm::eui64_pair_id(make_eui64(owlm::PRIMARY_MID_BYTES));
    auto const buf = make_test_packet(sender);
    auto const pkt = codec.decode(std::span<uint8_t const>(buf));
    EXPECT_TRUE(pkt.has_value());
    EXPECT_TRUE(codec.classify(*pkt, my_pair_id) == udptun::PacketRole::SelfRedundant);
}

TEST(owlm_codec_classify, self_legacy_arbitrary_mid)
{
    owlm::OwlmCodec const codec{};
    auto const sender = make_eui64(0xFFFE);  // legacy mid
    auto const my_pair_id = owlm::eui64_pair_id(sender);
    auto const buf = make_test_packet(sender);
    auto const pkt = codec.decode(std::span<uint8_t const>(buf));
    EXPECT_TRUE(pkt.has_value());
    EXPECT_TRUE(codec.classify(*pkt, my_pair_id) == udptun::PacketRole::SelfLegacy);
}

TEST(owlm_codec_classify, remote_primary)
{
    owlm::OwlmCodec const codec{};
    auto const sender = make_eui64(owlm::PRIMARY_MID_BYTES);
    // my_pair_id from a different OUI — clearly not us.
    ieee::Eui48 const other_mac{0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
    auto const my_pair_id = owlm::eui64_pair_id(owlm::make_owlm_eui64(other_mac, owlm::PRIMARY_MID_BYTES));
    auto const buf = make_test_packet(sender);
    auto const pkt = codec.decode(std::span<uint8_t const>(buf));
    EXPECT_TRUE(pkt.has_value());
    EXPECT_TRUE(codec.classify(*pkt, my_pair_id) == udptun::PacketRole::RemotePrimary);
}

TEST(owlm_codec_classify, remote_redundant)
{
    owlm::OwlmCodec const codec{};
    auto const sender = make_eui64(owlm::REDUNDANT_MID_BYTES);
    ieee::Eui48 const other_mac{0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
    auto const my_pair_id = owlm::eui64_pair_id(owlm::make_owlm_eui64(other_mac, owlm::PRIMARY_MID_BYTES));
    auto const buf = make_test_packet(sender);
    auto const pkt = codec.decode(std::span<uint8_t const>(buf));
    EXPECT_TRUE(pkt.has_value());
    EXPECT_TRUE(codec.classify(*pkt, my_pair_id) == udptun::PacketRole::RemoteRedundant);
}

TEST(owlm_codec_classify, remote_legacy_arbitrary_mid)
{
    owlm::OwlmCodec const codec{};
    auto const sender = make_eui64(0xFFFE);
    ieee::Eui48 const other_mac{0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
    auto const my_pair_id = owlm::eui64_pair_id(owlm::make_owlm_eui64(other_mac, 0xFFFE));
    auto const buf = make_test_packet(sender);
    auto const pkt = codec.decode(std::span<uint8_t const>(buf));
    EXPECT_TRUE(pkt.has_value());
    EXPECT_TRUE(codec.classify(*pkt, my_pair_id) == udptun::PacketRole::RemoteLegacy);
}

// ---------------------------------------------------------------------------
// sender_pair_id canonicalization — primary and redundant copies of the same
// logical sender must collapse to the same pair_id.
// ---------------------------------------------------------------------------

TEST(owlm_codec_pair_id, primary_and_redundant_share_pair_id)
{
    owlm::OwlmCodec const codec{};
    auto const primary_sender = make_eui64(owlm::PRIMARY_MID_BYTES);
    auto const redundant_sender = make_eui64(owlm::REDUNDANT_MID_BYTES);
    auto const buf_p = make_test_packet(primary_sender);
    auto const buf_r = make_test_packet(redundant_sender);
    auto const pkt_p = codec.decode(std::span<uint8_t const>(buf_p));
    auto const pkt_r = codec.decode(std::span<uint8_t const>(buf_r));
    EXPECT_TRUE(pkt_p.has_value());
    EXPECT_TRUE(pkt_r.has_value());
    EXPECT_EQ(codec.sender_pair_id(*pkt_p), codec.sender_pair_id(*pkt_r));
    // sender_id, however, must NOT be equal — they are distinct EUI-64s.
    EXPECT_NE(codec.sender_id(*pkt_p), codec.sender_id(*pkt_r));
}

TEST(owlm_codec_pair_id, legacy_pair_id_just_zeroes_mid)
{
    owlm::OwlmCodec const codec{};
    auto const sender = make_eui64(0xFFFE);
    auto const buf = make_test_packet(sender);
    auto const pkt = codec.decode(std::span<uint8_t const>(buf));
    EXPECT_TRUE(pkt.has_value());
    auto const pair = codec.sender_pair_id(*pkt);
    EXPECT_EQ(owlm::eui64_mid_bytes(pair), uint16_t{0});
}

TEST_MAIN(statusbar_owlm, owlm_codec_test)
