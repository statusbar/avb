#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// ATDECC module header including all format_to() overloads.
///
/// Include this header instead of atdecc.hpp when the translation unit needs
/// to call format_to on ATDECC types. Headers that only need the data
/// structures should include atdecc.hpp (or the specific sub-header) to avoid
/// the compile-time cost of <format>.

#include "statusbar/atdecc/atdecc.hpp"
#include "statusbar/atdecc/atdecc_acmp_format.hpp"
#include "statusbar/atdecc/atdecc_adp_format.hpp"
#include "statusbar/atdecc/atdecc_aecp_aa_format.hpp"
#include "statusbar/atdecc/atdecc_aecp_aem_format.hpp"
#include "statusbar/atdecc/atdecc_aecp_format.hpp"
#include "statusbar/atdecc/atdecc_aem_command_format.hpp"
#include "statusbar/atdecc/atdecc_aem_descriptor_format.hpp"
#include "statusbar/atdecc/atdecc_aem_format.hpp"
#include "statusbar/atdecc/atdecc_jdks_format.hpp"
#include "statusbar/atdecc/atdecc_print.hpp"
