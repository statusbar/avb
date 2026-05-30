#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// SRP module header — the MRP-family stream-reservation protocols.
///
/// Bundles MRP (the generic Multiple Registration Protocol base) and
/// its applications MSRP (stream reservation) and MVRP (VLAN
/// registration). Include the specific sub-header when only one
/// protocol is needed; include srp_format.hpp for the format_to()
/// overloads.

#include "statusbar/srp/srp_mrp.hpp"
#include "statusbar/srp/srp_msrp.hpp"
#include "statusbar/srp/srp_mvrp.hpp"
