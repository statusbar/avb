#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include <string_view>

namespace statusbar::udptun {

/// Source-level version identifier for the udptun framework.
/// Bumped when the Codec concept's required surface changes.
inline constexpr std::string_view udptun_version_string{"0.1.0"};

}  // namespace statusbar::udptun
