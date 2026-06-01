// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/stun/stun_server_sm.hpp"

#include "statusbar/crypto/aes_siv/aes128_siv.hpp"
#include "statusbar/ieee/ieee_ethernet.hpp"
#include "statusbar/net/net_address.hpp"
#include "statusbar/stun/stun_register.hpp"
#include "statusbar/stun/stun_server.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <cstdint>

using namespace statusbar::stun;

TEST(stun_server_sm, empty_to_one_registered)
{
    ServerSessionStateMachine sm{};
    ServerSessionContext ctx{};
    EXPECT_EQ(static_cast<int>(sm.current_state()), static_cast<int>(ServerDef::State::Empty));
    ctx.now_ns = 100;
    sm.handle_event(ctx, ServerDef::Event::FirstRegister);
    EXPECT_EQ(static_cast<int>(sm.current_state()), static_cast<int>(ServerDef::State::OneRegistered));
    EXPECT_EQ(ctx.last_event_ns, 100);
}

TEST(stun_server_sm, one_to_paired)
{
    ServerSessionStateMachine sm{};
    ServerSessionContext ctx{};
    sm.handle_event(ctx, ServerDef::Event::FirstRegister);
    ctx.now_ns = 200;
    sm.handle_event(ctx, ServerDef::Event::SecondRegister);
    EXPECT_EQ(static_cast<int>(sm.current_state()), static_cast<int>(ServerDef::State::Paired));
    EXPECT_EQ(ctx.last_event_ns, 200);
}

TEST(stun_server_sm, refresh_keeps_state)
{
    ServerSessionStateMachine sm{};
    ServerSessionContext ctx{};
    sm.handle_event(ctx, ServerDef::Event::FirstRegister);
    sm.handle_event(ctx, ServerDef::Event::RefreshOne);
    EXPECT_EQ(static_cast<int>(sm.current_state()), static_cast<int>(ServerDef::State::OneRegistered));
    sm.handle_event(ctx, ServerDef::Event::SecondRegister);
    sm.handle_event(ctx, ServerDef::Event::RefreshTwo);
    EXPECT_EQ(static_cast<int>(sm.current_state()), static_cast<int>(ServerDef::State::Paired));
}

TEST(stun_server_sm, expire_one_to_expired)
{
    ServerSessionStateMachine sm{};
    ServerSessionContext ctx{};
    sm.handle_event(ctx, ServerDef::Event::FirstRegister);
    sm.handle_event(ctx, ServerDef::Event::ExpireTick);
    EXPECT_EQ(static_cast<int>(sm.current_state()), static_cast<int>(ServerDef::State::Expired));
}

TEST(stun_server_sm, expire_paired_to_expired)
{
    ServerSessionStateMachine sm{};
    ServerSessionContext ctx{};
    sm.handle_event(ctx, ServerDef::Event::FirstRegister);
    sm.handle_event(ctx, ServerDef::Event::SecondRegister);
    sm.handle_event(ctx, ServerDef::Event::ExpireTick);
    EXPECT_EQ(static_cast<int>(sm.current_state()), static_cast<int>(ServerDef::State::Expired));
}

namespace {

auto make_test_key() -> statusbar::crypto::Aes128SivKey
{
    statusbar::crypto::Aes128SivKey k{};
    for (size_t i = 0; i < k.data.size(); ++i) {
        k.data[i] = static_cast<uint8_t>(0x40U + i);
    }
    return k;
}

auto make_session_id(uint8_t fill) -> SessionId
{
    SessionId s{};
    for (size_t i = 0; i < SESSION_ID_SIZE; ++i) {
        s.bytes[i] = fill;
    }
    return s;
}

}  // namespace

TEST(stun_server_core_mixed, mismatched_family_pair_is_rejected)
{
    auto const key = make_test_key();
    auto const sid = make_session_id(0x55);

    std::array<detail::SessionTableEntry, 4> table{};
    detail::ServerCore core{ServerConfig{.shared_key = key}, table};

    auto const v4_src = statusbar::net::SocketAddress::ipv4(192, 0, 2, 10, 40000);
    std::array<uint8_t, 16> const v6_bytes{0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x99};
    auto const v6_src = statusbar::net::SocketAddress::ipv6(v6_bytes, 40001);

    std::array<uint8_t, max_message_length> reply{};
    size_t reply_len = 0;

    // First client registers from an IPv4 source -> Waiting.
    {
        RegisterRequest r{};
        r.session_id = sid;
        r.client_eui64 = statusbar::ieee::Eui64{0, 0, 0, 0, 0, 0, 0, 1};
        std::array<uint8_t, max_message_length> req{};
        size_t req_len = 0;
        auto ec = encode_register_request(r, key, req, req_len);
        EXPECT_FALSE(static_cast<bool>(ec));
        (void)core.on_datagram(std::span<uint8_t const>{req.data(), req_len}, v4_src, 1000, reply, reply_len);
    }

    // Second client registers from an IPv6 source -> mismatched family -> error 470.
    {
        RegisterRequest r{};
        r.session_id = sid;
        r.client_eui64 = statusbar::ieee::Eui64{0, 0, 0, 0, 0, 0, 0, 2};
        std::array<uint8_t, max_message_length> req{};
        size_t req_len = 0;
        auto ec = encode_register_request(r, key, req, req_len);
        EXPECT_FALSE(static_cast<bool>(ec));
        reply_len = 0;
        bool const has_reply = core.on_datagram(std::span<uint8_t const>{req.data(), req_len}, v6_src, 2000, reply, reply_len);
        EXPECT_TRUE(has_reply);

        bool is_success = true;
        RegisterResponseSuccess ok{};
        RegisterResponseError err{};
        auto dec_ec = decode_register_response(std::span<uint8_t const>{reply.data(), reply_len}, key, is_success, ok, err);
        EXPECT_FALSE(static_cast<bool>(dec_ec));
        EXPECT_FALSE(is_success);
        EXPECT_EQ(err.error_code, uint16_t{470});
    }
}

TEST_MAIN(statusbar_stun, stun_server_sm_test)
