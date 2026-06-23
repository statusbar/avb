#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// avb_entity — turn-key AVB audio entities (AM824, stereo, dual-format AAF+AM824)
/// built on top of nanoavb + ptpclient. Module root that exports the public API.

#include "statusbar/avb_entity/avb_entity_aaf_reframe.hpp"
#include "statusbar/avb_entity/avb_entity_am824_io.hpp"
#include "statusbar/avb_entity/avb_entity_audio_io.hpp"
#include "statusbar/avb_entity/avb_entity_identity.hpp"
#include "statusbar/avb_entity/avb_entity_stereo_io.hpp"
#include "statusbar/avb_entity/log_sweep_generator.hpp"
#include "statusbar/avb_entity/tx_pcap_recorder.hpp"
