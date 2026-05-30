// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/srp/srp_mvrp.hpp"

namespace statusbar::srp::mvrp {

auto attribute_type_name(AttributeType type) noexcept -> char const*
{
    switch (type) {
        case AttributeType::VlanIdentifier:
            return "VlanIdentifier";
        default:
            return "Unknown";
    }
}

}  // namespace statusbar::srp::mvrp
