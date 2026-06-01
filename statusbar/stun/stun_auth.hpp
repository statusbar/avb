#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Authentication MIC for the private REGISTER protocol.
///
/// We use AES-128-SIV (RFC 5297) for symmetric authentication with
/// pre-shared keys. Plaintext is empty so the SIV reduces to a
/// deterministic 16-byte authentication tag computed over the wire
/// bytes used as AAD. Mirrors how RFC 8489 MESSAGE-INTEGRITY appends a
/// fixed-size attribute at the end of the message.

#include "statusbar/crypto/aes_siv/aes128_siv.hpp"
#include "statusbar/stun/stun_constants.hpp"
#include "statusbar/stun/stun_error.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>

namespace statusbar::stun {

using MicTag = std::array<uint8_t, MIC_SIZE>;

/// Append a STUN MIC attribute to the partially-built message buffer.
///
/// `cursor` points just past the last attribute written. The function
/// (1) temporarily patches the header `body_length` field to include
/// the about-to-be-written MIC TLV, (2) computes the SIV tag over bytes
/// [0..cursor) using AAD-only mode, and (3) appends the TLV.
///
/// `cursor` is updated to the new end on success.
[[nodiscard]] auto append_mic(std::span<uint8_t> buf, size_t& cursor, statusbar::crypto::Aes128SivKey const& key)
    -> std::error_code;

/// Verify a STUN MIC attribute. The caller passes the full datagram
/// (header + body, exactly as received). The MIC must be the last
/// attribute. On success, `inner_end_out` is set to the byte offset
/// where the MIC TLV begins (i.e. the end of the authenticated region
/// for any further attribute parsing).
[[nodiscard]] auto verify_mic(std::span<uint8_t const> datagram, statusbar::crypto::Aes128SivKey const& key, size_t& inner_end_out)
    -> std::error_code;

}  // namespace statusbar::stun
