#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include <string>
#include <system_error>

namespace statusbar::owlm {

enum class OwlmError
{
    InvalidMagic = 1,
    UnsupportedVersion,
    ReservedFlagsSet,
    DatagramTooShort,
    InvalidInterval,
    SenderNotSynced,
};

class OwlmErrorCategory : public std::error_category
{
  public:
    [[nodiscard]] auto name() const noexcept -> char const* override { return "statusbar.owlm"; }
    [[nodiscard]] auto message(int ev) const -> std::string override;
};

[[nodiscard]] auto owlm_error_category() noexcept -> std::error_category const&;

[[nodiscard]] auto make_error_code(OwlmError e) noexcept -> std::error_code;

}  // namespace statusbar::owlm

template <>
struct std::is_error_code_enum<statusbar::owlm::OwlmError> : std::true_type
{};
