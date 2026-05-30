// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Moved to statusbar/crypto/p256/p256_wire_constants.hpp
// This forwarding header maintains backward compatibility.

#pragma once

#include "statusbar/crypto/p256/p256_wire_constants.hpp"

namespace statusbar::crypto::avtp {

using ::statusbar::crypto::p256_curve_a_bytes;
using ::statusbar::crypto::p256_curve_b_bytes;
using ::statusbar::crypto::p256_field_prime_bytes;
using ::statusbar::crypto::p256_generator_x_bytes;
using ::statusbar::crypto::p256_generator_y_bytes;
using ::statusbar::crypto::p256_group_order_bytes;

}  // namespace statusbar::crypto::avtp
