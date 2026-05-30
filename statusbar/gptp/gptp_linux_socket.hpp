#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

//
// Linux raw-socket helpers shared by gPTP tools.
//
// This is the privileged-setup layer that sits below GptpClockOps:
// gPTP tools use these to open and configure the AF_PACKET socket
// (and, in HW mode, the /dev/ptpN device) before calling the
// make_linux_phc_clock_ops / make_linux_sw_clock_ops factories.
//
// All helpers print diagnostic messages to stderr on failure so the
// caller only needs to check the return value and bail out.
//

#if defined(__linux__)

#    include "statusbar/ieee/ieee.hpp"
#    include "statusbar/sm/sm.hpp"

#    include <cstdint>
#    include <span>

#    include <sys/socket.h>
#    include <sys/types.h>

namespace statusbar::gptp {

/// Network interface info needed to bind a gPTP socket and build outgoing frames.
struct GptpInterfaceInfo
{
    ieee::Eui48 mac{};
    int if_index{-1};
};

/// Look up the interface MAC + ifindex via SIOCGIFINDEX/SIOCGIFHWADDR.
/// On failure, returns an info with `if_index < 0` and prints to stderr.
[[nodiscard]] auto get_interface_info(int sock_fd, char const* ifname) -> GptpInterfaceInfo;

/// Open AF_PACKET/SOCK_RAW socket bound to the gPTP EtherType.
/// Returns the file descriptor, or -1 on failure (stderr message includes
/// a hint about CAP_NET_RAW).
[[nodiscard]] auto open_raw_gptp_socket() -> int;

/// Bind the socket to the interface and join the gPTP multicast group.
/// Returns true on success. Logs to stderr on failure.
[[nodiscard]] auto bind_gptp_socket(int sock_fd, int if_index) -> bool;

/// Enable hardware timestamping on the NIC via SIOCSHWTSTAMP
/// (HWTSTAMP_TX_ON + HWTSTAMP_FILTER_PTP_V2_EVENT).
[[nodiscard]] auto enable_hw_timestamping(int sock_fd, char const* ifname) -> bool;

/// Set SO_TIMESTAMPING for hardware TX+RX timestamps (RAW_HARDWARE).
[[nodiscard]] auto enable_so_timestamping_hw(int sock_fd) -> bool;

/// Set SO_TIMESTAMPING for software TX+RX timestamps. Used on NICs
/// without a PTP hardware clock (USB dongles, etc.).
[[nodiscard]] auto enable_so_timestamping_sw(int sock_fd) -> bool;

/// Discover the PHC device index for the interface via ETHTOOL_GET_TS_INFO,
/// then open /dev/ptpN. Returns the open fd, or -1 on failure.
[[nodiscard]] auto open_phc_for_interface(int sock_fd, char const* ifname) -> int;

/// Extract an SO_TIMESTAMPING timestamp from a recvmsg cmsg chain.
/// @param prefer_hw  If true, read ts[2] (RAW_HARDWARE); otherwise ts[0] (SOFTWARE).
/// Returns nanoseconds, or 0 if no timestamp cmsg was found.
[[nodiscard]] auto extract_so_timestamping_ns(struct msghdr const& msg, bool prefer_hw) -> int64_t;

/// Receive a gPTP frame and extract its RX timestamp.
///
/// Strips the Ethernet header and copies the payload into `payload_out`.
/// @returns Bytes copied into `payload_out` (capped to its size), or -1 on
///          error / would-block / undersized frame.
/// @param prefer_hw  If true, prefer the hardware timestamp slot; otherwise
///                   take the software one.
[[nodiscard]] auto recv_gptp_frame(int sock_fd, std::span<uint8_t> payload_out, int64_t& rx_ns_out, bool prefer_hw) -> ssize_t;

/// Read CLOCK_REALTIME as nanoseconds. Used by the SW timestamping path.
[[nodiscard]] auto clock_realtime_ns() -> int64_t;

/// Compute a poll() timeout from the port's next deadline, capped.
/// `TimePoint::max()` (no deadline) returns `cap_ms`.
[[nodiscard]] auto compute_poll_timeout_ms(sm::TimePoint deadline, sm::TimePoint now, int cap_ms = 100) -> int;

}  // namespace statusbar::gptp

#endif  // __linux__
