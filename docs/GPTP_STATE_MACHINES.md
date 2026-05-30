<!-- Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com> -->

# gPTP Slave-Role Follower (IEEE 802.1AS)

This document describes the slave-only IEEE 802.1AS time synchronization
implementation in `statusbar/gptp/`.

## Scope

- **Slave-role only.** No grandmaster, no BMCA election, no master
  transmit path (no Sync or Announce generation).
- **Two profiles:**
  1. **Standard IEEE 802.1AS-2020** — full Pdelay exchange, dynamic
     asCapable, BMCA informational only.
  2. **AVnu Automotive Profile** — `as_capable_initial=true`,
     `bmca_enabled=false`, aggressive sync interval (−5 = 31.25 ms),
     optional manual peer delay with Pdelay protocol disabled.
- **Platform-agnostic core.** The state machines, servo, and protocol
  logic depend only on `GptpClockOps` (a set of `std::function`
  callbacks). No OS headers in the core.
- **Two deployment targets:**
  1. **Linux** — `SO_TIMESTAMPING` + PHC via `clock_adjtime`.
     A `GptpClockOps::linux_raw_socket_and_phc()` factory is provided
     in a separate TU (Phase 7, deferred to Linux host).
  2. **Bare metal** — user implements the `GptpClockOps` callbacks
     against their hardware timer peripheral.

Reference implementation: OpenAvnu gPTP daemon at
`stash/avnu/gptp/common/` (ptp_message.cpp, ieee1588clock.cpp,
ether_port.cpp). Line-number citations throughout the code point
back to those files.

## Module layout

```
statusbar/gptp/
├── gptp.hpp                     module facade
├── gptp_base.hpp/.cpp           base types (Timestamp, SourcePortIdentity, ClockQuality)
├── gptp_header.hpp              MessageHeader (34 bytes, bit-packed)
├── gptp_messages.hpp/.cpp       Sync/FollowUp/Pdelay/Announce/Signaling wire structs + parser
├── gptp_tlv.hpp                 ScaledNs, TlvHeader, FollowUpInformationTLV, MessageIntervalRequestTLV
├── gptp_error.hpp/.cpp          GptpError enum + std::error_code category
├── gptp_config.hpp/.cpp         GptpConfig, Profile, PdelayMode, PhyDelay, factories + validate()
├── gptp_clock_ops.hpp           GptpClockOps std::function interface + TxResult + LinkSpeedMbps
│
├── gptp_port_state_sm.hpp/.cpp  PortStateSM (statusbar::sm) — 6 states
├── gptp_md_sync_receive.hpp     MDSyncReceive — Sync+FollowUp pairing
├── gptp_md_pdelay_req.hpp/.cpp  MDPdelayReq — 4-timestamp Pdelay exchange + link delay math
├── gptp_link_delay.hpp/.cpp     Pure functions: compute_link_delay_ns, compute_neighbor_rate_ratio
├── gptp_port_announce_receive.hpp  Informational Announce latch (no BMCA)
├── gptp_servo.hpp/.cpp          PI controller ported from OpenAvnu
│
├── gptp_slave_port.hpp/.cpp     GptpSlavePort — per-NIC orchestrator
├── gptp_slave_port_test.cpp     Integration tests
├── gptp_test.cpp                Wire-format + config unit tests
├── gptp_ntpshm.hpp/.cpp         NTP SHM reader (for ptp4l interop, unchanged)
└── gptp_read_ntpshm_tool.cpp    CLI tool for NTP SHM (unchanged)
```

## Components

### PortStateSM

6-state lifecycle via `statusbar::sm::TransitionTable`:

```
Start → Disabled → Initializing → Listening → Uncalibrated → Slave
```

Events: `LinkUp`, `LinkDown`, `AsCapableAcquired`, `AsCapableLost`,
`FirstSyncLocked`, `SyncLost`, `AdministrativeDisable`.

Profile behavior: In Automotive mode with `as_capable_initial=true`,
the driver fires two `AsCapableAcquired` events on `LinkUp` to bypass
`Listening` and jump straight to `Uncalibrated`.

### MDSyncReceive

Pairs Sync + FollowUp messages by `sequenceId`. A new Sync arriving
while WaitingForFollowUp discards the prior pending pair (per Clause
10.2.7). The output is an `MDSyncReceiveIndication` carrying:

- `preciseOriginTimestamp` (from FollowUp body)
- `correction_field_ns` (sum of Sync + FollowUp header corrections)
- `cumulative_scaled_rate_offset` (from FollowUp Information TLV)
- `gm_time_base_indicator`, `last_gm_phase_change`, `scaled_last_gm_freq_change`
- `sync_rx_local_ns` (local HW timestamp of Sync arrival, PHY-adjusted)
- `log_message_interval`

### MDPdelayReq

Drives the requestor side of the 4-timestamp Pdelay exchange. States:

```
NotEnabled → Initial → WaitingForPdelayResp → WaitingForPdelayRespFollowUp → WaitingForPdelayIntervalTimer
```

Outputs `MDPdelayMeasurement` with `mean_link_delay_ns` and
`neighbor_rate_ratio`. Tracks `as_capable` via
`lost_pdelay_resp_threshold`.

When `PdelayMode::Disabled`, the FSM stays in `NotEnabled` and the
port uses `config.manual_peer_delay_ns` / `manual_neighbor_rate_ratio`.

#### Link delay math (gptp_link_delay.hpp)

```
meanLinkDelay = ((t4 - t1) - neighborRateRatio × (t3 - t2)) / 2
neighborRateRatio = (t1_curr - t1_prev) / (t2_curr - t2_prev)
```

Both clamped to ±250 ppm from 1.0 by default.

### PortAnnounceReceive

Informational only. Latches the most recent Announce's grandmaster
identity, priority, clock quality, UTC offset, steps removed, and
time source. Expires on announce-receipt timeout. Fires
`on_grandmaster_change` observer callback on identity changes
(including when the Announce times out and the GM is "lost").

### ServoLoop (PI controller)

Ported from OpenAvnu `ieee1588clock.cpp:306-440`. Consumes one
`MDSyncReceiveIndication` per call:

1. **Correction application** (ptp_message.cpp:1046-1071):
   ```
   master_local_freq_offset = (1 + cumulativeScaledRateOffset / 2^41) / neighborRateRatio
   correction = meanLinkDelay × master_local_freq_offset + followUpCorrectionField
   corrected_master_ns = preciseOriginTimestamp + correction
   ```

2. **Phase error**: `corrected_master_ns - sync_rx_local_ns`

3. **Negative time jump detection**: if master time went backward
   between two consecutive syncs, suppress the servo for one cycle.

4. **Phase jump**: if `|phase_error| > threshold` for
   `N consecutive samples`, call `adjust_phase_ns()` and reset
   the PI integrator.

5. **PI controller** (ieee1588clock.cpp:423-434):
   ```
   syncPerSec = 2^(-logSyncInterval)
   ppm += ki × syncPerSec × phase_error + kp × (rate - 1.0) × 1e6
   ppm = clamp(ppm, -servo_ppm_limit, +servo_ppm_limit)
   ```
   Output: `adjust_frequency_ppb(ppm × 1000)`

### GptpSlavePort

Per-NIC orchestrator. Owns all components, observer slot table,
timer deadlines, and the `GptpClockOps` callbacks.

**API surface** (same shape as `MsrpParticipant`):

| Method | Description |
|--------|-------------|
| `start(now, link_up)` | Fire UCT, arm timers, optionally bring up link |
| `stop()` | Disable all FSMs, cancel timers |
| `subscribe(Observer)` / `unsubscribe(id)` | Fixed-capacity slot-based observer registration |
| `receive_frame(payload, rx_hw_ts, now)` | Parse + dispatch to the right component |
| `report_tx_timestamp(type, seq_id, tx_ns)` | Deferred TX timestamp feedback |
| `on_link_up(now)` / `on_link_down(now)` | Link state hints |
| `tick(now)` | Process expired timers |
| `next_deadline()` | Earliest timer deadline for event-loop scheduling |
| `is_synchronized()` | true in Slave state |
| `last_master_offset_ns()` | Phase error from last servo pass |
| `last_rate_ratio()` | Master-to-local clock rate ratio |
| `mean_link_delay_ns()` | Latest peer link delay |
| `neighbor_rate_ratio()` | Latest peer rate ratio |
| `as_capable()` | Current asCapable state |
| `grandmaster_identity()` | Latched GM ClockIdentity |
| `listener_permits_transmit(stream_id)` | (on MsrpParticipant, not here) |

**Observer callbacks:**

| Callback | When |
|----------|------|
| `on_sync_state_change(bool)` | Synchronized ↔ unsynchronized transitions |
| `on_sync_update(offset_ns, rate)` | Every successful Sync+FollowUp pair |
| `on_grandmaster_change(ClockIdentity)` | GM identity changed or timed out |
| `on_peer_delay_update(delay_ns, rate)` | After each successful Pdelay exchange |
| `on_as_capable_change(bool)` | asCapable acquired or lost |

## Configuration

`GptpConfig` struct with two factories:

```cpp
auto cfg = GptpConfig::standard_defaults();
// or:
auto cfg = GptpConfig::avnu_automotive_slave_defaults();
```

Key fields:

| Field | Standard | Automotive | Description |
|-------|----------|-----------|-------------|
| `profile` | Standard | AvnuAutomotive | Profile enum |
| `initial_log_sync_interval` | −3 (125ms) | −5 (31.25ms) | Sync period |
| `bmca_enabled` | true | false | Announce drives state? |
| `as_capable_initial` | false | true | Pre-grant asCapable? |
| `pdelay_mode` | Active | Active or Disabled | Pdelay behavior |
| `manual_peer_delay_ns` | 0 | user-set | Static delay if pdelay off |
| `verify_source_port_identity` | true | false | Check Sync source? |
| `allow_negative_correction_field` | false | true | Accept −ve corrections? |
| `servo_integral_gain` | 0.0003 | 0.0003 | PI integral (OpenAvnu INTEGRAL) |
| `servo_proportional_gain` | 1.0 | 1.0 | PI proportional (OpenAvnu PROPORTIONAL) |
| `servo_ppm_limit` | 250.0 | 250.0 | Frequency clamp ±ppm |
| `servo_phase_jump_threshold_ns` | 1e9 | 1e9 | Phase jump threshold (1 sec) |

PHY delay compensation per link speed (default: 1G = 184/382 ns,
100M = 1044/2133 ns) and all timeouts / thresholds are also
configurable.

## Clock ops interface

```cpp
struct GptpClockOps {
    std::function<int64_t()>                                      get_local_time_ns;
    std::function<TxResult(std::span<uint8_t const> payload)>     send_frame;
    std::function<void(int64_t phase_ns)>                         adjust_phase_ns;
    std::function<void(double ppb)>                               adjust_frequency_ppb;
    std::function<LinkSpeedMbps()>                                get_link_speed;
};
```

**Linux**: implement via raw `AF_PACKET` socket with `SO_TIMESTAMPING`
for HW timestamps, and `clock_adjtime()` on the PHC fd for
adjustments. A factory (`GptpClockOps::linux_raw_socket_and_phc()`)
is provided in a separate translation unit.

**Bare metal**: bind lambdas to your hardware timer peripheral's
read/write/adjust registers.

**Software-only** (tests): bind lambdas to a `TestClock` struct that
tracks a synthetic nanosecond counter.

## Testing

- **Wire format**: ScaledNs round-trip, FollowUpInformationTLV
  round-trip, MessageIntervalRequestTLV round-trip, SignalingMessage
  parse, GptpConfig validation.
- **Link delay math**: known-symmetric-delay (500 ns), neighbor
  rate ratio from consecutive exchanges (1.0), rate ratio clamping.
- **Servo**: convergence with zero offset, phase jump detection with
  configurable consecutive sample count.
- **Integration**: synthetic GM drives a GptpSlavePort through startup
  (Automotive with pre-asCapable → Uncalibrated → Slave on first
  sync), observer lifecycle, sync receipt timeout → sync loss →
  re-enter Uncalibrated, Announce latching.

All 135 tests pass.

## References

- IEEE 802.1AS-2020 Clause 10 (gPTP state machines), Clause 11.2
  (media-dependent functions), Clause 8.1 (rate ratio definitions)
- OpenAvnu ptp_message.cpp:890-1844 (Sync/FollowUp/Pdelay processing)
- OpenAvnu ieee1588clock.cpp:306-440 (PI servo)
- OpenAvnu ether_port.cpp:104-484 (Automotive Profile conditionals)
- AVnu Automotive Profile README (`stash/avnu/gptp/README_AVNU_AP.txt`)