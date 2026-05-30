#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// gPTP module header including all format_to() overloads.
///
/// Include this header instead of gptp.hpp when the translation unit
/// needs to call format_to on gPTP types. Headers that only need the
/// data structures should include gptp.hpp (or the specific sub-header)
/// to avoid the compile-time cost of <format>.

#include "statusbar/gptp/gptp.hpp"
#include "statusbar/gptp/gptp_base_format.hpp"
#include "statusbar/gptp/gptp_header_format.hpp"
#include "statusbar/gptp/gptp_messages_format.hpp"
