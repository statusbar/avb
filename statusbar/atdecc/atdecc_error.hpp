#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include <system_error>

namespace statusbar::atdecc {

///
/// Error codes for IEEE 1722.1 (ATDECC) protocol operations.
///
/// Library-layer errors raised by the atdecc module. Prefer these over
/// generic `std::errc` values when reporting ATDECC-specific failures:
/// they give callers a self-documenting taxonomy instead of borrowed
/// POSIX errno semantics that may not fit the domain.
///
/// CLI tools (e.g. `atdecc_acmp_controller_tool.cpp`) that validate
/// user input at program startup may continue to use `std::errc::invalid_argument`
/// since that value is semantically a good match for "bad CLI argument."
///
enum class AtdeccError
{
    /// A variable-length protocol element (TLV, descriptor list, frame)
    /// ran past the end of its containing buffer during parsing.
    /// The wire data was truncated or the length field was wrong.
    truncated = 1,
};

///
/// Error category for ATDECC errors.
///
class AtdeccErrorCategory : public std::error_category
{
  public:
    [[nodiscard]] auto name() const noexcept -> char const* override { return "statusbar.atdecc"; }

    [[nodiscard]] auto message(int ev) const -> std::string override;
};

///
/// Get the global ATDECC error category instance.
///
[[nodiscard]] auto atdecc_error_category() noexcept -> std::error_category const&;

///
/// Create an error_code from an AtdeccError.
///
[[nodiscard]] auto make_error_code(AtdeccError e) noexcept -> std::error_code;

}  // namespace statusbar::atdecc

///
/// Register AtdeccError as an error_code enum.
///
template <>
struct std::is_error_code_enum<statusbar::atdecc::AtdeccError> : std::true_type
{};
