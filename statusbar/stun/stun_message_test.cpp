// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/stun/stun_message.hpp"

#include "statusbar/net/net_address.hpp"
#include "statusbar/stun/stun_constants.hpp"
#include "statusbar/stun/stun_error.hpp"
#include "statusbar/stun/stun_types.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

using namespace statusbar;
using namespace statusbar::stun;

namespace {

auto sample_txid() -> TransactionId
{
    TransactionId t{};
    for (uint8_t i = 0; i < TRANSACTION_ID_SIZE; ++i) {
        t.bytes[i] = static_cast<uint8_t>(0x10U + i);
    }
    return t;
}

}  // namespace

TEST(stun_msg_header, round_trip_binding_request)
{
    MessageHeader h{.method = Method::Binding, .klass = Class::Request, .body_length = 0, .transaction_id = sample_txid()};
    std::array<uint8_t, HEADER_SIZE> buf{};
    EXPECT_FALSE(static_cast<bool>(encode_header(h, buf)));

    EXPECT_EQ(buf[0], uint8_t{0x00});
    EXPECT_EQ(buf[1], uint8_t{0x01});  // method 0x001, class 0x00
    EXPECT_EQ(buf[2], uint8_t{0x00});
    EXPECT_EQ(buf[3], uint8_t{0x00});
    EXPECT_EQ(buf[4], uint8_t{0x21});
    EXPECT_EQ(buf[5], uint8_t{0x12});
    EXPECT_EQ(buf[6], uint8_t{0xA4});
    EXPECT_EQ(buf[7], uint8_t{0x42});

    MessageHeader d{};
    EXPECT_FALSE(static_cast<bool>(decode_header(buf, d)));
    EXPECT_EQ(static_cast<int>(d.method), static_cast<int>(Method::Binding));
    EXPECT_EQ(static_cast<int>(d.klass), static_cast<int>(Class::Request));
    EXPECT_EQ(d.body_length, uint16_t{0});
    EXPECT_TRUE(d.transaction_id == h.transaction_id);
}

TEST(stun_msg_header, round_trip_register_success_response)
{
    MessageHeader h{
        .method = Method::Register, .klass = Class::SuccessResponse, .body_length = 16, .transaction_id = sample_txid()};
    std::array<uint8_t, HEADER_SIZE> buf{};
    EXPECT_FALSE(static_cast<bool>(encode_header(h, buf)));

    MessageHeader d{};
    EXPECT_FALSE(static_cast<bool>(decode_header(buf, d)));
    EXPECT_EQ(static_cast<int>(d.method), static_cast<int>(Method::Register));
    EXPECT_EQ(static_cast<int>(d.klass), static_cast<int>(Class::SuccessResponse));
    EXPECT_EQ(d.body_length, uint16_t{16});
}

TEST(stun_msg_header, reject_bad_magic)
{
    MessageHeader h{.method = Method::Binding, .klass = Class::Request, .body_length = 0, .transaction_id = sample_txid()};
    std::array<uint8_t, HEADER_SIZE> buf{};
    (void)encode_header(h, buf);
    buf[4] = 0x42;  // corrupt magic

    MessageHeader d{};
    auto ec = decode_header(buf, d);
    EXPECT_EQ(ec, make_error_code(StunError::InvalidMagicCookie));
}

TEST(stun_msg_header, reject_too_short)
{
    std::array<uint8_t, 19> buf{};
    MessageHeader d{};
    auto ec = decode_header(buf, d);
    EXPECT_EQ(ec, make_error_code(StunError::DatagramTooShort));
}

TEST(stun_msg_header, reject_non_zero_leading_bits)
{
    MessageHeader h{.method = Method::Binding, .klass = Class::Request, .body_length = 0, .transaction_id = sample_txid()};
    std::array<uint8_t, HEADER_SIZE> buf{};
    (void)encode_header(h, buf);
    buf[0] = 0x80;  // set top bit

    MessageHeader d{};
    auto ec = decode_header(buf, d);
    EXPECT_EQ(ec, make_error_code(StunError::InvalidLeadingBits));
}

TEST(stun_msg_header, reject_unaligned_length)
{
    MessageHeader h{.method = Method::Binding, .klass = Class::Request, .body_length = 0, .transaction_id = sample_txid()};
    std::array<uint8_t, HEADER_SIZE> buf{};
    (void)encode_header(h, buf);
    buf[3] = 0x05;  // length=5, not multiple of 4

    MessageHeader d{};
    auto ec = decode_header(buf, d);
    EXPECT_EQ(ec, make_error_code(StunError::InvalidMessageLength));
}

TEST(stun_attr, append_and_iterate)
{
    std::array<uint8_t, 64> buf{};
    size_t cursor = 0;
    std::array<uint8_t, 3> v1{0xAA, 0xBB, 0xCC};
    EXPECT_FALSE(static_cast<bool>(append_attribute(buf, cursor, 0x8030U, v1)));
    EXPECT_EQ(cursor, size_t{4 + 4});  // 4-byte TLV header + 4 (3 padded to 4)
    std::array<uint8_t, 4> v2{0xDE, 0xAD, 0xBE, 0xEF};
    EXPECT_FALSE(static_cast<bool>(append_attribute(buf, cursor, 0x8031U, v2)));
    EXPECT_EQ(cursor, size_t{8 + 8});

    AttributeIterator it{std::span<uint8_t const>{buf.data(), cursor}};
    uint16_t at = 0;
    std::span<uint8_t const> val{};
    bool done = false;

    EXPECT_FALSE(static_cast<bool>(it.next(at, val, done)));
    EXPECT_FALSE(done);
    EXPECT_EQ(at, uint16_t{0x8030});
    EXPECT_EQ(val.size(), size_t{3});
    EXPECT_EQ(val[0], uint8_t{0xAA});
    EXPECT_EQ(val[2], uint8_t{0xCC});

    EXPECT_FALSE(static_cast<bool>(it.next(at, val, done)));
    EXPECT_FALSE(done);
    EXPECT_EQ(at, uint16_t{0x8031});
    EXPECT_EQ(val.size(), size_t{4});

    EXPECT_FALSE(static_cast<bool>(it.next(at, val, done)));
    EXPECT_TRUE(done);
}

TEST(stun_attr, truncated_attribute_detected)
{
    std::array<uint8_t, 4> tlv_only_header{0x80, 0x30, 0x00, 0x10};  // length=16 but no payload
    AttributeIterator it{tlv_only_header};
    uint16_t at = 0;
    std::span<uint8_t const> val{};
    bool done = false;
    auto ec = it.next(at, val, done);
    EXPECT_EQ(ec, make_error_code(StunError::AttributeTruncated));
}

TEST(stun_xor_mapped, ipv4_round_trip)
{
    auto txid = sample_txid();
    auto addr = statusbar::net::SocketAddress::ipv4(192, 0, 2, 7, 51020);

    std::array<uint8_t, 64> buf{};
    size_t cursor = 0;
    EXPECT_FALSE(static_cast<bool>(
        append_xor_mapped_address(buf, cursor, static_cast<uint16_t>(AttributeType::XorMappedAddress), addr, txid)));

    AttributeIterator it{std::span<uint8_t const>{buf.data(), cursor}};
    uint16_t at = 0;
    std::span<uint8_t const> val{};
    bool done = false;
    EXPECT_FALSE(static_cast<bool>(it.next(at, val, done)));
    EXPECT_EQ(at, static_cast<uint16_t>(AttributeType::XorMappedAddress));

    statusbar::net::SocketAddress decoded{};
    EXPECT_FALSE(static_cast<bool>(decode_xor_mapped_address(val, txid, decoded)));
    EXPECT_EQ(decoded.family(), AF_INET);
    EXPECT_EQ(decoded.port(), uint16_t{51020});
}

TEST(stun_xor_mapped, ipv6_round_trip)
{
    auto txid = sample_txid();
    std::array<uint8_t, 16> v6_addr{
        0x20,
        0x01,
        0x0d,
        0xb8,  // 2001:db8::
        0x00,
        0x00,
        0x00,
        0x00,  //
        0x00,
        0x00,
        0x00,
        0x00,  //
        0x00,
        0x00,
        0x00,
        0x07,  // ::7
    };
    auto addr = statusbar::net::SocketAddress::ipv6(v6_addr, 51020);

    std::array<uint8_t, 64> buf{};
    size_t cursor = 0;
    EXPECT_FALSE(static_cast<bool>(
        append_xor_mapped_address(buf, cursor, static_cast<uint16_t>(AttributeType::XorMappedAddress), addr, txid)));

    AttributeIterator it{std::span<uint8_t const>{buf.data(), cursor}};
    uint16_t at = 0;
    std::span<uint8_t const> val{};
    bool done = false;
    EXPECT_FALSE(static_cast<bool>(it.next(at, val, done)));
    EXPECT_EQ(at, static_cast<uint16_t>(AttributeType::XorMappedAddress));
    EXPECT_EQ(val.size(), size_t{20});

    statusbar::net::SocketAddress decoded{};
    EXPECT_FALSE(static_cast<bool>(decode_xor_mapped_address(val, txid, decoded)));
    EXPECT_EQ(decoded.family(), AF_INET6);
    EXPECT_EQ(decoded.port(), uint16_t{51020});
}

TEST(stun_xor_mapped, ipv6_xor_uses_transaction_id)
{
    // Two messages with the same v6 address but different transaction
    // ids must produce different on-wire bytes (per RFC 8489 §14.2: the
    // bytes 4..15 of the X-Address are XORed with the transaction id).
    std::array<uint8_t, 16> v6_addr{
        0x20,
        0x01,
        0x0d,
        0xb8,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x07,
    };
    auto addr = statusbar::net::SocketAddress::ipv6(v6_addr, 1234);

    TransactionId txid_a{};
    txid_a.bytes[0] = 0xAA;
    TransactionId txid_b{};
    txid_b.bytes[0] = 0xBB;

    std::array<uint8_t, 64> buf_a{};
    std::array<uint8_t, 64> buf_b{};
    size_t ca = 0;
    size_t cb = 0;
    (void)append_xor_mapped_address(buf_a, ca, 0x0020U, addr, txid_a);
    (void)append_xor_mapped_address(buf_b, cb, 0x0020U, addr, txid_b);

    // bytes 4..7 of the value are the cookie-XOR region (same in both);
    // bytes 8..19 are the transaction-id-XOR region. The first byte
    // there reflects txid byte 0 which differs between the two cases.
    EXPECT_NE(buf_a[4 + 8], buf_b[4 + 8]);
}

TEST(stun_attr, attribute_count_cap_rejects_seventeenth_attribute)
{
    // Build a body with 17 trivial 0-byte-value attributes. The
    // iterator should refuse to walk beyond max_attributes_per_message.
    std::array<uint8_t, 4 * 17> buf{};
    size_t cursor = 0;
    for (int i = 0; i < 17; ++i) {
        std::span<uint8_t const> empty{};
        EXPECT_FALSE(static_cast<bool>(append_attribute(buf, cursor, 0x8040U, empty)));
    }
    AttributeIterator it{std::span<uint8_t const>{buf.data(), cursor}};
    int seen = 0;
    while (true) {
        uint16_t at = 0;
        std::span<uint8_t const> val{};
        bool done = false;
        auto ec = it.next(at, val, done);
        if (ec) {
            EXPECT_EQ(seen, static_cast<int>(max_attributes_per_message));
            EXPECT_EQ(ec, make_error_code(StunError::AttributeTruncated));
            return;
        }
        if (done) {
            break;
        }
        ++seen;
    }
    EXPECT_TRUE(false);  // expected to hit the cap before "done"
}

TEST(stun_xor_mapped, ipv4_xor_obfuscation)
{
    auto txid = sample_txid();
    // Per RFC 5769 / RFC 8489 examples: the encoded port and address bytes
    // must be the host values XORed with the magic cookie. Here we just
    // sanity-check that the wire bytes do NOT match the host port/address
    // bytes — i.e. the XOR is actually applied.
    auto addr = statusbar::net::SocketAddress::ipv4(192, 0, 2, 7, 0xCAFE);
    std::array<uint8_t, 64> buf{};
    size_t cursor = 0;
    (void)append_xor_mapped_address(buf, cursor, static_cast<uint16_t>(AttributeType::XorMappedAddress), addr, txid);

    // value starts at offset 4 (TLV header). bytes 2..3 = X-Port.
    uint16_t const x_port = static_cast<uint16_t>((buf[4 + 2] << 8) | buf[4 + 3]);
    EXPECT_NE(x_port, uint16_t{0xCAFE});
    EXPECT_EQ(x_port ^ PORT_XOR_MASK, uint16_t{0xCAFE});
}

TEST(stun_error_code, round_trip)
{
    std::array<uint8_t, 64> buf{};
    size_t cursor = 0;
    EXPECT_FALSE(static_cast<bool>(append_error_code(buf, cursor, 486, "session full")));

    AttributeIterator it{std::span<uint8_t const>{buf.data(), cursor}};
    uint16_t at = 0;
    std::span<uint8_t const> val{};
    bool done = false;
    EXPECT_FALSE(static_cast<bool>(it.next(at, val, done)));
    EXPECT_EQ(at, static_cast<uint16_t>(AttributeType::ErrorCode));

    uint16_t code = 0;
    std::span<uint8_t const> reason{};
    EXPECT_FALSE(static_cast<bool>(decode_error_code(val, code, reason)));
    EXPECT_EQ(code, uint16_t{486});
    std::string_view reason_sv{reinterpret_cast<char const*>(reason.data()), reason.size()};
    EXPECT_TRUE(reason_sv == std::string_view{"session full"});
}

TEST(stun_error_code, reject_invalid_code)
{
    std::array<uint8_t, 64> buf{};
    size_t cursor = 0;
    auto ec = append_error_code(buf, cursor, 50, "bad");
    EXPECT_EQ(ec, make_error_code(StunError::InvalidErrorCode));
}

TEST_MAIN(statusbar_stun, stun_message_test)
