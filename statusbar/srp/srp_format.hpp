#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// MSRP module header including all format_to() overloads for the
/// MRP / MSRP / MVRP types.
///
/// Include this header when the translation unit needs to call
/// format_to on MRP-family types. Headers that only need the data
/// structures should include the specific sub-header (mrp.hpp,
/// msrp.hpp, mvrp.hpp) to avoid the compile-time cost of <format>.

#include "statusbar/srp/srp_mrp_format.hpp"
#include "statusbar/srp/srp_msrp_format.hpp"
#include "statusbar/srp/srp_mvrp_format.hpp"
