// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/stun/stun_register.hpp"

#include "statusbar/crypto/aes_siv/aes128_siv.hpp"
#include "statusbar/net/net_address.hpp"
#include "statusbar/stun/stun_constants.hpp"
#include "statusbar/stun/stun_error.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <cstdint>

using namespace statusbar;
using namespace statusbar::stun;

namespace {

auto make_test_key() -> statusbar::crypto::Aes128SivKey
{
    statusbar::crypto::Aes128SivKey k{};
    for (size_t i = 0; i < k.data.size(); ++i) {
        k.data[i] = static_cast<uint8_t>(0x40U + i);
    }
    return k;
}

auto make_session_id() -> SessionId
{
    SessionId s{};
    for (size_t i = 0; i < SESSION_ID_SIZE; ++i) {
        s.bytes[i] = static_cast<uint8_t>(0xA0U + i);
    }
    return s;
}

auto make_txid() -> TransactionId
{
    TransactionId t{};
    for (size_t i = 0; i < TRANSACTION_ID_SIZE; ++i) {
        t.bytes[i] = static_cast<uint8_t>(0x10U + i);
    }
    return t;
}

}  // namespace

TEST(stun_register, request_round_trip)
{
    RegisterRequest req{
        .transaction_id = make_txid(),
        .session_id = make_session_id(),
        .client_eui64 = ieee::Eui64{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11},
        .role = Role::Initiator,
    };
    auto key = make_test_key();
    std::array<uint8_t, max_message_length> buf{};
    size_t written = 0;
    EXPECT_FALSE(static_cast<bool>(encode_register_request(req, key, buf, written)));
    EXPECT_TRUE(written > HEADER_SIZE);

    RegisterRequest decoded{};
    auto ec = decode_register_request(std::span<uint8_t const>{buf.data(), written}, key, decoded);
    EXPECT_FALSE(static_cast<bool>(ec));
    EXPECT_TRUE(decoded.transaction_id == req.transaction_id);
    EXPECT_TRUE(decoded.session_id == req.session_id);
    EXPECT_EQ(decoded.client_eui64.to_uint64(), req.client_eui64.to_uint64());
    EXPECT_EQ(static_cast<int>(decoded.role), static_cast<int>(req.role));
}

TEST(stun_register, request_rejects_bad_key)
{
    RegisterRequest req{
        .transaction_id = make_txid(),
        .session_id = make_session_id(),
        .client_eui64 = ieee::Eui64{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11},
        .role = Role::Initiator,
    };
    auto key = make_test_key();
    std::array<uint8_t, max_message_length> buf{};
    size_t written = 0;
    (void)encode_register_request(req, key, buf, written);

    auto bad_key = key;
    bad_key.data[0] ^= 0x01;
    RegisterRequest decoded{};
    auto ec = decode_register_request(std::span<uint8_t const>{buf.data(), written}, bad_key, decoded);
    EXPECT_EQ(ec, make_error_code(StunError::AuthFailed));
}

TEST(stun_register, request_rejects_tamper)
{
    RegisterRequest req{
        .transaction_id = make_txid(),
        .session_id = make_session_id(),
        .client_eui64 = ieee::Eui64{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11},
        .role = Role::Responder,
    };
    auto key = make_test_key();
    std::array<uint8_t, max_message_length> buf{};
    size_t written = 0;
    (void)encode_register_request(req, key, buf, written);

    buf[HEADER_SIZE + 4] ^= 0x40;  // flip a bit inside SESSION-ID value
    RegisterRequest decoded{};
    auto ec = decode_register_request(std::span<uint8_t const>{buf.data(), written}, key, decoded);
    EXPECT_EQ(ec, make_error_code(StunError::AuthFailed));
}

TEST(stun_register, response_waiting_round_trip)
{
    RegisterResponseSuccess resp{
        .transaction_id = make_txid(),
        .session_id = make_session_id(),
        .xor_mapped_address = statusbar::net::SocketAddress::ipv4(203, 0, 113, 7, 40000),
        .state = SessionState::Waiting,
        .refresh_interval_ms = 12345,
    };
    auto key = make_test_key();
    std::array<uint8_t, max_message_length> buf{};
    size_t written = 0;
    EXPECT_FALSE(static_cast<bool>(encode_register_response_success(resp, key, buf, written)));

    bool is_success = false;
    RegisterResponseSuccess success{};
    RegisterResponseError err{};
    auto ec = decode_register_response(std::span<uint8_t const>{buf.data(), written}, key, is_success, success, err);
    EXPECT_FALSE(static_cast<bool>(ec));
    EXPECT_TRUE(is_success);
    EXPECT_EQ(static_cast<int>(success.state), static_cast<int>(SessionState::Waiting));
    EXPECT_EQ(success.refresh_interval_ms, uint32_t{12345});
    EXPECT_EQ(success.xor_mapped_address.port(), uint16_t{40000});
    EXPECT_FALSE(success.peer_eui64.has_value());
    EXPECT_FALSE(success.peer_xor_mapped_address.has_value());
}

TEST(stun_register, response_paired_round_trip)
{
    RegisterResponseSuccess resp{
        .transaction_id = make_txid(),
        .session_id = make_session_id(),
        .xor_mapped_address = statusbar::net::SocketAddress::ipv4(203, 0, 113, 7, 40000),
        .state = SessionState::Paired,
        .refresh_interval_ms = default_refresh_interval_ms,
        .peer_eui64 = ieee::Eui64{1, 2, 3, 4, 5, 6, 7, 8},
        .peer_xor_mapped_address = statusbar::net::SocketAddress::ipv4(198, 51, 100, 9, 50000),
    };
    auto key = make_test_key();
    std::array<uint8_t, max_message_length> buf{};
    size_t written = 0;
    EXPECT_FALSE(static_cast<bool>(encode_register_response_success(resp, key, buf, written)));

    bool is_success = false;
    RegisterResponseSuccess success{};
    RegisterResponseError err{};
    EXPECT_FALSE(
        static_cast<bool>(decode_register_response(std::span<uint8_t const>{buf.data(), written}, key, is_success, success, err)));
    EXPECT_TRUE(is_success);
    EXPECT_EQ(static_cast<int>(success.state), static_cast<int>(SessionState::Paired));
    EXPECT_TRUE(success.peer_eui64.has_value());
    EXPECT_EQ(success.peer_eui64->to_uint64(), uint64_t{0x0102030405060708ULL});
    EXPECT_TRUE(success.peer_xor_mapped_address.has_value());
    EXPECT_EQ(success.peer_xor_mapped_address->port(), uint16_t{50000});
}

TEST(stun_register, response_error_round_trip)
{
    RegisterResponseError resp{.transaction_id = make_txid(), .error_code = 486};
    auto key = make_test_key();
    std::array<uint8_t, max_message_length> buf{};
    size_t written = 0;
    EXPECT_FALSE(static_cast<bool>(encode_register_response_error(resp, "session full", key, buf, written)));

    bool is_success = true;
    RegisterResponseSuccess success{};
    RegisterResponseError err{};
    EXPECT_FALSE(
        static_cast<bool>(decode_register_response(std::span<uint8_t const>{buf.data(), written}, key, is_success, success, err)));
    EXPECT_FALSE(is_success);
    EXPECT_EQ(err.error_code, uint16_t{486});
}

TEST_MAIN(statusbar_stun, stun_register_test)
