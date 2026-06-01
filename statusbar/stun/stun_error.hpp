#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include <string>
#include <system_error>

namespace statusbar::stun {

enum class StunError
{
    DatagramTooShort = 1,
    InvalidLeadingBits,
    InvalidMagicCookie,
    InvalidMessageLength,
    InvalidAttributeLength,
    AttributeTruncated,
    UnknownComprehensionRequiredAttribute,
    DuplicateAttribute,
    MissingRequiredAttribute,
    InvalidAddressFamily,
    UnsupportedAddressFamily,
    BufferTooSmall,
    AuthFailed,
    AuthenticationMissing,
    InvalidSessionState,
    InvalidRole,
    SessionFull,
    SessionNotFound,
    SessionExpired,
    TransactionMismatch,
    UnexpectedClass,
    UnexpectedMethod,
    SocketCreateFailed,
    SocketBindFailed,
    SocketSendFailed,
    NoServerAddress,
    InvalidErrorCode,
};

class StunErrorCategory : public std::error_category
{
  public:
    [[nodiscard]] auto name() const noexcept -> char const* override { return "statusbar.stun"; }
    [[nodiscard]] auto message(int ev) const -> std::string override;
};

[[nodiscard]] auto stun_error_category() noexcept -> std::error_category const&;

[[nodiscard]] auto make_error_code(StunError e) noexcept -> std::error_code;

}  // namespace statusbar::stun

template <>
struct std::is_error_code_enum<statusbar::stun::StunError> : std::true_type
{};
