#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_identity.hpp"

#include <string>

#if defined(__linux__)
#    include "statusbar/ptpclient/ptpclient_setup.hpp"
#endif

namespace statusbar::udptun {

/// Selects which clock domain udptun stamps onto the wire.
///   - GptpSlave: start statusbar's own pure-C++ gPTP slave on
///     `identity.gptp_session_config.interface`. The slave disciplines
///     a `gptp::GptpTimeBridge` (offset-only `mraw → master`).
///   - Ptp4l (Linux only): consume a kernel PHC kept in sync by an
///     external `ptp4l` daemon. `ptpclient::setup_ptp_app_with_fallback`
///     opens the device, starts a regression bridge, and blocks until
///     the bridge is healthy.
///   - Realtime: skip any master clock and stamp the wire with
///     `CLOCK_REALTIME`. Both endpoints are assumed roughly NTP-synced;
///     the only supported mode on non-Linux platforms.
enum class TimeSourceKind
{
    GptpSlave,
    Ptp4l,
    Realtime,
};

/// Combined configuration for the wire-time source. Only the fields
/// matching `kind` are consulted; the rest are ignored.
struct TimeSourceConfig
{
    TimeSourceKind kind{TimeSourceKind::GptpSlave};

    /// GptpSlave: gPTP slave session + fallback interface for MAC.
    LocalIdentityConfig identity{};

#if defined(__linux__)
    /// Ptp4l: PTP client / bridge / sampling parameters.
    ptpclient::PtpAppConfig ptp4l{};
#endif

    /// Ptp4l / Realtime: interface to read the local MAC from. Empty
    /// falls back to `identity.gptp_session_config.interface` (Linux)
    /// or `identity.fallback_interface`.
    std::string mac_interface{};
};

/// Read the MAC of `interface_name` directly via SIOCGIFHWADDR (Linux)
/// or the platform equivalent. Used by Ptp4l / Realtime time sources
/// where no gPTP slave is around to supply `local_mac()`. Returns
/// `std::nullopt` and logs a stderr diagnostic on failure.
[[nodiscard]] auto read_mac_from_interface(std::string const& interface_name) -> std::optional<ieee::Eui48>;

}  // namespace statusbar::udptun
