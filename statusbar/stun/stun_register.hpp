#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// High-level REGISTER request / response builders. Wraps the generic
/// header + attribute codec with our private REGISTER method semantics.

#include "statusbar/crypto/aes_siv/aes128_siv.hpp"
#include "statusbar/ieee/ieee_ethernet.hpp"
#include "statusbar/net/net_address.hpp"
#include "statusbar/stun/stun_constants.hpp"
#include "statusbar/stun/stun_error.hpp"
#include "statusbar/stun/stun_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <system_error>

namespace statusbar::stun {

struct RegisterRequest
{
    TransactionId transaction_id{};
    SessionId session_id{};
    statusbar::ieee::Eui64 client_eui64{};
    Role role{Role::Initiator};
};

struct RegisterResponseSuccess
{
    TransactionId transaction_id{};
    SessionId session_id{};
    statusbar::net::SocketAddress xor_mapped_address{};
    SessionState state{SessionState::Waiting};
    uint32_t refresh_interval_ms{default_refresh_interval_ms};

    /// Present once the server has paired the session.
    std::optional<statusbar::ieee::Eui64> peer_eui64{};
    std::optional<statusbar::net::SocketAddress> peer_xor_mapped_address{};
};

struct RegisterResponseError
{
    TransactionId transaction_id{};
    uint16_t error_code{0};
};

/// Encode a register request including MIC. Returns total bytes written.
[[nodiscard]] auto encode_register_request(
    RegisterRequest const& req, statusbar::crypto::Aes128SivKey const& key, std::span<uint8_t> buf, size_t& written_out)
    -> std::error_code;

/// Encode a success register response including MIC.
[[nodiscard]] auto encode_register_response_success(
    RegisterResponseSuccess const& resp, statusbar::crypto::Aes128SivKey const& key, std::span<uint8_t> buf, size_t& written_out)
    -> std::error_code;

/// Encode an error register response. The transaction id field of `resp`
/// is echoed back so the client can match it to the outstanding request.
[[nodiscard]] auto encode_register_response_error(
    RegisterResponseError const& resp,
    std::string_view reason,
    statusbar::crypto::Aes128SivKey const& key,
    std::span<uint8_t> buf,
    size_t& written_out) -> std::error_code;

/// Decode and authenticate an incoming REGISTER request. Returns an
/// error_code on parse / auth failure; populates `out` on success.
[[nodiscard]] auto decode_register_request(
    std::span<uint8_t const> datagram, statusbar::crypto::Aes128SivKey const& key, RegisterRequest& out) -> std::error_code;

/// Decode and authenticate a register response. Returns the parsed
/// success or error response in the appropriate output. Caller inspects
/// `is_success_out` to know which is populated.
[[nodiscard]] auto decode_register_response(
    std::span<uint8_t const> datagram,
    statusbar::crypto::Aes128SivKey const& key,
    bool& is_success_out,
    RegisterResponseSuccess& success_out,
    RegisterResponseError& error_out) -> std::error_code;

}  // namespace statusbar::stun
