// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/atdecc/atdecc_error.hpp"

#include <string>
#include <system_error>

namespace statusbar::atdecc {

auto AtdeccErrorCategory::message(int ev) const -> std::string
{
    switch (static_cast<AtdeccError>(ev)) {
        case AtdeccError::truncated:
            return "ATDECC wire data truncated (length field inconsistent with buffer size)";
        default:
            return "Unknown ATDECC error";
    }
}

auto atdecc_error_category() noexcept -> std::error_category const&
{
    static AtdeccErrorCategory const instance;
    return instance;
}

auto make_error_code(AtdeccError e) noexcept -> std::error_code
{
    return {static_cast<int>(e), atdecc_error_category()};
}

}  // namespace statusbar::atdecc
