#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/stun/stun_constants.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <span>

namespace statusbar::stun {

/// 12-byte STUN transaction ID. Treated as opaque bytes — RFC 8489 says
/// implementations should fill it with cryptographically random bytes
/// (we leave that policy to the caller).
struct TransactionId
{
    std::array<uint8_t, TRANSACTION_ID_SIZE> bytes{};

    [[nodiscard]] auto span() const noexcept -> std::span<uint8_t const, TRANSACTION_ID_SIZE>
    {
        return std::span<uint8_t const, TRANSACTION_ID_SIZE>{bytes};
    }

    [[nodiscard]] auto operator==(TransactionId const&) const noexcept -> bool = default;
};

/// 16-byte session identifier. Both peers in a pair use the same value;
/// the server pairs entries by this id.
struct SessionId
{
    std::array<uint8_t, SESSION_ID_SIZE> bytes{};

    [[nodiscard]] auto operator==(SessionId const&) const noexcept -> bool = default;
    [[nodiscard]] auto operator<=>(SessionId const&) const noexcept = default;
};

}  // namespace statusbar::stun
