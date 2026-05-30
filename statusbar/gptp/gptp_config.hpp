#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

//
// GptpConfig — fixed-at-construction configuration for a slave-role
// IEEE 802.1AS follower port.
//
// All capacity limits, servo tuning, interval defaults, PHY delay
// compensation, and profile flags are carried here. The participant
// reserves all dynamic containers to these limits at construction so
// that no heap allocation occurs during steady-state protocol
// processing.
//
// Two factory methods are provided for the two supported profiles:
//   - standard_defaults()               — IEEE 802.1AS-2020
//   - avnu_automotive_slave_defaults()  — AVnu Automotive Profile slave
//
// The Automotive Profile diverges from standard in 20+ behavioral
// details (see stash/avnu/gptp/common/ether_port.cpp). The factory
// captures all of them; callers usually start from the factory and
// override individual fields as needed.
//

#include "statusbar/gptp/gptp_error.hpp"
#include "statusbar/status/status.hpp"
#include "statusbar/tsn/tsn_clock_identity.hpp"

#include <cstddef>
#include <cstdint>

namespace statusbar::gptp {

/// Supported 802.1AS profiles. Only slave role is implemented; the
/// profile selects behavioral defaults (BMCA on/off, initial asCapable,
/// Pdelay active/manual, sync interval, etc.).
enum class Profile : uint8_t
{
    /// IEEE 802.1AS-2020 standard behavior. Dynamic asCapable via
    /// Pdelay, BMCA-driven grandmaster selection, standard sync interval
    /// (−3 = 125 ms), standard Pdelay interval (0 = 1 s).
    Standard,

    /// AVnu Automotive Profile (slave). asCapable=true at startup,
    /// no BMCA verification, aggressive sync interval (−5 = 31.25 ms),
    /// Pdelay may be disabled with manual peer delay.
    AvnuAutomotive,
};

/// Pdelay protocol mode.
///
/// The Automotive Profile allows a slave to operate without running
/// the Pdelay protocol at all; in that case the caller supplies a
/// static meanLinkDelay and neighborRateRatio via config.
enum class PdelayMode : uint8_t
{
    /// Full 802.1AS Pdelay exchange: we initiate Pdelay_Req every
    /// `log_pdelay_interval` and respond to peer Pdelay_Reqs. The link
    /// delay and neighbor rate ratio are measured live.
    Active,

    /// We respond to peer Pdelay_Reqs (to be polite to the rest of the
    /// network) but never initiate. meanLinkDelay comes from
    /// `manual_peer_delay_ns`; neighborRateRatio from
    /// `manual_neighbor_rate_ratio`.
    RespondOnly,

    /// We do not participate in Pdelay at all. No Pdelay_Reqs are sent
    /// and no Pdelay_Reqs are answered. meanLinkDelay and
    /// neighborRateRatio come from config.
    Disabled,
};

/// Per-link-speed PHY delay compensation (subtracted from rx
/// hardware timestamps before use, added to tx hardware timestamps).
/// See stash/avnu/gptp/common/ether_port.cpp:231-237 for the
/// compensation rule.
struct PhyDelay
{
    int64_t tx_ns{0};
    int64_t rx_ns{0};
};

/// Complete slave-port configuration.
struct GptpConfig
{
    // ======================================================
    // Identity
    // ======================================================

    /// The local port's clock identity — used in source port identity
    /// for Pdelay_Req and Pdelay_Resp, and for matching Pdelay_Resp
    /// against our own requests.
    tsn::ClockIdentity local_clock_identity{};

    /// The 802.1Q port number (1-based). A device with multiple
    /// physical ports assigns a distinct number to each.
    uint16_t local_port_number{1};

    // ======================================================
    // Profile and role
    // ======================================================

    Profile profile{Profile::Standard};

    // ======================================================
    // Intervals (log2 seconds)
    // ======================================================

    /// Sync interval on startup before Signaling renegotiation.
    /// Standard: −3 (125 ms). Automotive: −5 (31.25 ms).
    int8_t initial_log_sync_interval{-3};

    /// Operational sync interval after Signaling / BMCA settles.
    /// Typically the same as initial for Automotive Profile.
    int8_t oper_log_sync_interval{-3};

    /// Pdelay_Req interval on startup.
    /// Standard: 0 (1 s). Automotive: may be 0 or disabled.
    int8_t initial_log_pdelay_interval{0};

    int8_t oper_log_pdelay_interval{0};

    /// Announce interval (ignored by the informational
    /// PortAnnounceReceiveSM — we only use it for timeout tracking).
    int8_t initial_log_announce_interval{0};

    // ======================================================
    // Timeouts (in units of the corresponding interval)
    // ======================================================

    /// SyncReceiptTimeout: declare sync loss after this many consecutive
    /// missed Sync messages. IEEE 802.1AS default is 3.
    uint8_t sync_receipt_timeout_multiplier{3};

    /// Number of consecutive missed Pdelay_Resp before declaring the
    /// exchange lost. OpenAvnu default (lostPdelayRespThresh) is 3.
    uint16_t lost_pdelay_resp_threshold{3};

    /// Number of sequential Pdelay_Resp sequence id mismatches before
    /// asCapable=false. OpenAvnu default (seqIdAsCapableThresh) is 2.
    uint8_t seq_id_as_capable_threshold{2};

    /// AnnounceReceiptTimeout multiplier. Declare GM lost after this
    /// many missed Announces.
    uint8_t announce_receipt_timeout_multiplier{3};

    // ======================================================
    // Protocol thresholds
    // ======================================================

    /// Reject link delay measurements above this threshold. In Standard
    /// profile this sets asCapable=false; in Automotive profile the
    /// threshold is ignored (OpenAvnu ether_port.cpp:1820).
    int64_t neighbor_prop_delay_threshold_ns{800};

    /// Whether to accept negative correctionField values. Standard
    /// rejects; Automotive accepts.
    bool allow_negative_correction_field{false};

    /// Whether to verify that the received Sync's source port identity
    /// matches the expected grandmaster port. Automotive disables.
    bool verify_source_port_identity{true};

    // ======================================================
    // Profile behavior flags
    // ======================================================

    /// If false, BMCA election does not run and Announce messages are
    /// parsed for diagnostic display only. We are slave-only either
    /// way; this flag only controls whether Announce drives the port
    /// state transitions (vs. just latching GM info).
    bool bmca_enabled{true};

    /// If true, asCapable is set immediately on link up without
    /// waiting for a successful Pdelay exchange. Automotive sets this
    /// to true.
    bool as_capable_initial{false};

    // ======================================================
    // Pdelay configuration
    // ======================================================

    PdelayMode pdelay_mode{PdelayMode::Active};

    /// Used as the meanLinkDelay when pdelay_mode != Active, or as the
    /// initial value before the first successful live measurement.
    int64_t manual_peer_delay_ns{0};

    /// Used as the neighborRateRatio when pdelay_mode != Active.
    /// Default 1.0 means "assume peer clock runs at the same rate as
    /// ours". A calibrated static value can be supplied if known.
    double manual_neighbor_rate_ratio{1.0};

    // ======================================================
    // PHY delay compensation (per link speed)
    // ======================================================

    /// Defaults match OpenAvnu gptp_cfg.ini: 1 Gbps = 184/382,
    /// 100 Mbps = 1044/2133, 10 Mbps = 0/0 (unknown).
    PhyDelay phy_delay_1g{.tx_ns = 184, .rx_ns = 382};
    PhyDelay phy_delay_100m{.tx_ns = 1044, .rx_ns = 2133};
    PhyDelay phy_delay_10m{};

    // ======================================================
    // Servo tuning (PI controller — see Phase 4e)
    // ======================================================

    /// PI integral gain. OpenAvnu default (ieee1588clock.cpp:48-49):
    /// INTEGRAL = 0.0003.
    double servo_integral_gain{0.0003};

    /// PI proportional gain. OpenAvnu default: PROPORTIONAL = 1.0.
    double servo_proportional_gain{1.0};

    /// Frequency adjustment clamp in ppm. OpenAvnu default: ±250 ppm.
    double servo_ppm_limit{250.0};

    /// Phase error threshold for issuing a jump instead of slew-only
    /// correction. Default: 50 µs. When the absolute phase error
    /// exceeds this for servo_phase_jump_consecutive_samples consecutive
    /// syncs, the clock is stepped rather than slewed.
    int64_t servo_phase_jump_threshold_ns{50'000};

    /// Number of consecutive samples with |phase error| above the
    /// threshold required to trigger a jump. OpenAvnu default: 6.
    uint8_t servo_phase_jump_consecutive_samples{6};

    // ======================================================
    // Capacity limits — drive fixed-size containers in the
    // participant. Reserve()d at construction, no reallocation.
    // ======================================================

    /// Maximum in-flight Sync→FollowUp pairs. Typically 1-2 is
    /// sufficient even under heavy sync rate; 4 leaves headroom.
    size_t max_pending_sync{4};

    /// Maximum in-flight Pdelay exchanges.
    size_t max_pending_pdelay{4};

    /// Maximum simultaneous Observer subscriptions on the port.
    size_t max_observers{4};

    // ======================================================
    // Factories
    // ======================================================

    /// IEEE 802.1AS-2020 standard profile defaults.
    [[nodiscard]] static auto standard_defaults() noexcept -> GptpConfig;

    /// AVnu Automotive Profile slave defaults. All behavioral flags
    /// set to slave-side automotive values per README_AVNU_AP.txt.
    [[nodiscard]] static auto avnu_automotive_slave_defaults() noexcept -> GptpConfig;

    // ======================================================
    // Validation
    // ======================================================

    /// Check the config for self-consistency. Returns a GptpError if
    /// any invariant is violated.
    [[nodiscard]] auto validate() const noexcept -> Status;
};

}  // namespace statusbar::gptp
