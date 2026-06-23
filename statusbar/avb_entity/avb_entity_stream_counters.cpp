// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avb_entity/avb_entity_stream_counters.hpp"

namespace statusbar::avb_entity {

void tally_stream_input_packet(
    StreamInputCounters& c,
    uint8_t const seq,
    uint32_t const avtp_ts,
    bool const tv,
    bool const tu,
    bool const mr,
    bool const format_ok,
    uint64_t const samples_per_ch,
    bool const ts_sparse,
    uint64_t const gptp_now_ns,
    uint32_t const lock_tolerance_ns,
    uint32_t const sample_rate_hz)
{
    auto const relaxed = std::memory_order_relaxed;
    c.frames_rx.fetch_add(1, relaxed);
    if (!format_ok) {
        c.unsupported_format.fetch_add(1, relaxed);
        return;
    }

    // Sequence-number continuity (8-bit wrap).
    if (c.have_prev && seq != static_cast<uint8_t>(c.prev_seq + 1U)) {
        c.seq_num_mismatch.fetch_add(1, relaxed);
    }
    // Media clock restart: the talker toggles mr when its media clock resets.
    if (c.have_prev && mr != c.prev_mr) {
        c.media_reset.fetch_add(1, relaxed);
    }

    if (tv) {
        c.timestamp_valid.fetch_add(1, relaxed);
        if (tu) {
            c.timestamp_uncertain.fetch_add(1, relaxed);
        }
        // LATE/EARLY vs gPTP-now (lower 32 bits of ns). The reactor clock is
        // monotonic, so the caller passes last_gptp_ns_ published by the media
        // timer (<=125us stale, fine against ms-scale thresholds). diff<0 =>
        // presentation time already passed (late); diff implausibly large =>
        // stamped too early. gptp_now_ns==0 means "unknown" -> skip.
        constexpr uint32_t EARLY_THRESHOLD_NS = 50'000'000;  // 50 ms
        if (gptp_now_ns != 0) {
            int32_t const diff = static_cast<int32_t>(avtp_ts - static_cast<uint32_t>(gptp_now_ns));
            if (diff < 0) {
                c.late_timestamp.fetch_add(1, relaxed);
            } else if (static_cast<uint32_t>(diff) > EARLY_THRESHOLD_NS) {
                c.early_timestamp.fetch_add(1, relaxed);
            }
        }
        // Media lock = a STEADY inter-(valid-)timestamp STEP. AAF advances
        // 125 us/packet (12 samples @ 96 kHz). AM824's avtp_timestamp instead
        // follows the IEC 61883-6 SYT cadence: SYT_INTERVAL=16 @ 96 kHz, so a valid
        // timestamp appears on 3 of every 4 (12-sample) packets and the valid stamps
        // step a constant 166.67 us. So we lock on the step being CONSTANT, not on a
        // hardcoded per-packet value (which only ever matched AAF). Both ends are
        // gPTP-slaved so the step is correct by construction; jitter/drops change it
        // and unlock. A coarse plausibility window vs the nominal packet period
        // rejects garbage so two equal junk steps cannot false-lock.
        constexpr int LOCK_RUN_THRESHOLD = 8;  // ~1 ms (AAF) / ~1.3 ms (AM824) of clean steps
        if (c.have_prev_ts && samples_per_ch > 0) {
            uint32_t const actual = avtp_ts - c.prev_ts;                                    // wrap-safe (uint32)
            uint64_t const nominal = (samples_per_ch * 1'000'000'000ULL) / sample_rate_hz;  // 125 us @ 12/96k
            bool const plausible = (actual >= (nominal / 2)) && (actual < (nominal * 4));
            uint64_t const step_err = (actual > c.prev_delta) ? (actual - c.prev_delta) : (c.prev_delta - actual);
            if (plausible && c.have_prev_delta && step_err <= lock_tolerance_ns) {
                if (c.locked_run < LOCK_RUN_THRESHOLD) {
                    ++c.locked_run;
                }
                if (c.locked_run >= LOCK_RUN_THRESHOLD && !c.is_locked) {
                    c.is_locked = true;
                    c.media_locked.fetch_add(1, relaxed);
                }
            } else {
                c.locked_run = 0;
                if (c.is_locked) {
                    c.is_locked = false;
                    c.media_unlocked.fetch_add(1, relaxed);
                }
            }
            c.prev_delta = actual;
            c.have_prev_delta = true;
        }
        c.prev_ts = avtp_ts;
        c.have_prev_ts = true;
    } else if (!ts_sparse) {
        // tv=0 is a real fault for a format that timestamps every packet (AAF).
        // For IEC 61883-6 (AM824) tv=0 between SYT-bearing packets is the normal
        // SYT_INTERVAL cadence (see the media-lock note above), so ts_sparse
        // streams do NOT tally it -- it would otherwise climb ~1/4 of FRAMES_RX
        // on a perfectly healthy, media-locked AM824 stream and read as an error.
        c.timestamp_not_valid.fetch_add(1, relaxed);
    }

    c.prev_seq = seq;
    c.prev_mr = mr;
    c.have_prev = true;
}

}  // namespace statusbar::avb_entity
