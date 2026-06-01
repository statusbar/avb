#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/ieee/ieee.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#if defined(__linux__)
#    include "statusbar/gptp/gptp_slave_session.hpp"
#endif

namespace statusbar::udptun {

/// Inputs to `setup_local_identity`: how to discover the local MAC and
/// whether to start a gPTP slave session.
struct LocalIdentityConfig
{
    /// Skip gPTP entirely. The framework will read the local MAC
    /// directly from `fallback_interface` and stamp the wire timeline
    /// with CLOCK_REALTIME. This is the only supported mode on
    /// non-Linux platforms.
    bool no_gptp{false};

    /// Interface to read MAC from when `no_gptp` is true. If empty,
    /// `gptp_session_config.interface` is used as the fallback.
    std::string fallback_interface{};

#if defined(__linux__)
    /// gPTP slave session configuration (only consulted when
    /// `no_gptp == false`). The `interface` field doubles as the
    /// fallback when `fallback_interface` is empty.
    gptp::SlaveSessionConfig gptp_session_config{};
#endif
};

#if defined(__linux__)
/// Resolve the local 48-bit MAC. In gPTP mode the SlaveSession is
/// constructed and started in `session` and its `local_mac()` is
/// returned. In `no_gptp` mode the MAC is read directly from the
/// interface and `session` is left empty. Returns `std::nullopt` on
/// any failure (interface lookup, session start, etc.) with a stderr
/// diagnostic.
[[nodiscard]] auto setup_local_identity(LocalIdentityConfig const& cfg, std::optional<gptp::SlaveSession>& session)
    -> std::optional<ieee::Eui48>;
#endif

/// gPTP-less variant: always reads the MAC from `cfg.fallback_interface`
/// (or `gptp_session_config.interface` on Linux as a fallback). Forces
/// `cfg.no_gptp == true` regardless of the input — this is what the
/// non-Linux Session ctor calls. Available on all platforms.
[[nodiscard]] auto setup_local_identity_no_gptp(LocalIdentityConfig const& cfg) -> std::optional<ieee::Eui48>;

/// A primary/redundant identity pair for a udptun TX session. Both
/// identities are 8-byte sender IDs in EUI-64 form (OWLM uses the
/// EUI-64 mid bytes to distinguish primary from redundant; AAF / Annex J
/// use distinct stream_ids per ST 2022-7 convention but the wire shape
/// is identical). When `redundant_enabled == false`, `redundant_id` is
/// unused and `temporal_shift_ns` is zero.
struct TxIdentityPair
{
    ieee::Eui64 primary_id{};
    ieee::Eui64 redundant_id{};
    bool redundant_enabled{false};
    int64_t temporal_shift_ns{0};
};

/// Print a one-line "<label> primary EUI-64: …" stderr banner, plus a
/// second "<label> redundant EUI-64: … (temporal_shift=Nms)" line when
/// `redundant_enabled` is set. Useful for live operator confirmation
/// at startup. `label` is a short tool name like "OWLM" or "AAF".
void print_eui64_banner(std::string_view label, TxIdentityPair const& tx, int64_t temporal_shift_ms);

}  // namespace statusbar::udptun
