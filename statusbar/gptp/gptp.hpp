#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/gptp/gptp_base.hpp"
#include "statusbar/gptp/gptp_clock_ops.hpp"
#include "statusbar/gptp/gptp_config.hpp"
#include "statusbar/gptp/gptp_error.hpp"
#include "statusbar/gptp/gptp_header.hpp"
#include "statusbar/gptp/gptp_messages.hpp"
#include "statusbar/gptp/gptp_soft_clock.hpp"
#include "statusbar/gptp/gptp_time_bridge.hpp"
#include "statusbar/gptp/gptp_tlv.hpp"

// gptp_ntpshm.hpp is intentionally NOT included here: its lower half is a
// ptpclient driver, so pulling it into the gptp module header would give every
// gptp consumer a hidden header dependency on ptpclient (and dsp). Code
// that needs the NTP SHM clock source includes gptp_ntpshm.hpp directly.
