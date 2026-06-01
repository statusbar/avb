#pragma once

/// \file netdump.hpp
/// \brief Network packet dump and pretty-print module.
///
/// Provides formatting functions that dispatch on EtherType and IP protocol
/// to produce human-readable text output for captured Ethernet frames.
/// Supports IPv4, IPv6, ARP, ICMP, IGMP, UDP, gPTP, AVTP, MSRP, MVRP,
/// and ATDECC protocols. All formatters use output iterators for
/// zero-allocation streaming output.

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/netdump/netdump_format.hpp"
