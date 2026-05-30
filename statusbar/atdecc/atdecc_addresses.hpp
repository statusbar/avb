#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// ATDECC Standard Addresses
/// IEEE 1722.1-2021 multicast addresses - canonical definitions in ieee::protocols

#include "statusbar/ieee/ieee.hpp"

#include <cstdint>

namespace statusbar::atdecc {

/// Alias to canonical definition in ieee::protocols::ATDECC_MULTICAST_MAC
inline constexpr auto const& ATDECC_MULTICAST_MAC = ieee::protocols::ATDECC_MULTICAST_MAC;

/// Alias to canonical definition in ieee::protocols::ATDECC_IDENTIFY_MULTICAST_MAC
inline constexpr auto const& ATDECC_IDENTIFY_MULTICAST_MAC = ieee::protocols::ATDECC_IDENTIFY_MULTICAST_MAC;

}  // namespace statusbar::atdecc
