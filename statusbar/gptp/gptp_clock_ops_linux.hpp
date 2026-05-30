#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

//
// GptpClockOps Linux factory — binds the platform-agnostic GptpClockOps
// interface to Linux raw AF_PACKET sockets with SO_TIMESTAMPING and
// PTP Hardware Clock (PHC) kernel calls.
//
// The factory takes two already-open file descriptors:
//   - raw_sock_fd: AF_PACKET socket bound to the gPTP interface with
//                  SO_TIMESTAMPING enabled (TX+RX hardware timestamps)
//   - phc_fd:      /dev/ptpN device for the NIC's hardware clock
//
// The caller is responsible for the privileged setup (socket creation,
// NIC timestamping enable via SIOCSHWTSTAMP, PHC device open) because
// those steps require CAP_NET_RAW / CAP_NET_ADMIN and are deployment-
// specific. The factory only binds the lambdas.
//

#if defined(__linux__)

#    include "statusbar/gptp/gptp_clock_ops.hpp"
#    include "statusbar/gptp/gptp_soft_clock.hpp"
#    include "statusbar/ieee/ieee.hpp"

#    include <string>

namespace statusbar::gptp {

/// Build a GptpClockOps whose lambdas call Linux PHC and raw-socket
/// system calls.
///
/// @param raw_sock_fd  AF_PACKET socket with SO_TIMESTAMPING enabled.
///                     Used for send_frame (sendto + MSG_ERRQUEUE TX
///                     timestamp retrieval).
/// @param phc_fd       Open file descriptor to /dev/ptpN. Used for
///                     clock_gettime (via FD_TO_CLOCKID), clock_adjtime
///                     (frequency + phase).
/// @param ifname       Interface name (e.g. "eth0") for ethtool link
///                     speed query.
/// @param if_index     Interface index (from if_nametoindex) for
///                     sockaddr_ll when sending.
/// @param src_mac      Local MAC address of the interface. Used to
///                     build the Ethernet header on outgoing frames.
[[nodiscard]] auto make_linux_phc_clock_ops(int raw_sock_fd, int phc_fd, std::string ifname, int if_index, ieee::Eui48 src_mac)
    -> GptpClockOps;

/// Hardware-timestamping factory that does NOT discipline the PHC.
/// PHC stays at its raw (boot-relative) value; the servo's phase /
/// frequency corrections accumulate in `soft_clock`. The lambdas read
/// raw PHC + virtual offset for `get_local_time_ns`, and apply
/// `soft_clock.correct_timestamp()` to wire timestamps before they
/// enter the protocol stack. This mirrors the existing
/// `make_linux_sw_clock_ops` pattern, but keeps PHC-domain hardware
/// timestamping accuracy.
///
/// Use case: applications where master-time consumption stays inside
/// the process (via `bridge.gptp_now()` / `correct_timestamp`) and
/// the system PHC must not be touched (no ptp4l-style discipline).
///
/// IMPORTANT: matching the SW factory's contract, the event loop must
/// call `soft_clock.correct_timestamp()` on every RX hardware
/// timestamp BEFORE passing it to GptpSlavePort::receive_frame().
///
/// @param raw_sock_fd  AF_PACKET socket with SO_TIMESTAMPING (HW flags).
/// @param phc_fd       Open file descriptor to /dev/ptpN. Read-only
///                     in this mode — never written via clock_adjtime
///                     or clock_settime.
/// @param soft_clock   Shared virtual clock. The returned lambdas
///                     mutate it on adjust_phase / adjust_frequency
///                     and read it on get_local_time / send_frame.
/// @param ifname       Interface name for ethtool link speed query.
/// @param if_index     Interface index for sockaddr_ll.
/// @param src_mac      Local MAC address.
[[nodiscard]] auto make_linux_phc_passthrough_ops(
    int raw_sock_fd, int phc_fd, SoftClock& soft_clock, std::string ifname, int if_index, ieee::Eui48 src_mac) -> GptpClockOps;

/// Build a GptpClockOps for software-only timestamping (no PHC).
///
/// For systems without a PTP hardware clock — e.g. USB Ethernet
/// dongles on Asahi Linux. Uses SO_TIMESTAMPING with SOFTWARE flags
/// and a SoftClock virtual clock that the servo steers instead of a
/// hardware oscillator.
///
/// The `soft_clock` must outlive the returned GptpClockOps (and is
/// shared with the application, which reads PTP time via
/// `soft_clock.get_ptp_time_ns()`).
///
/// IMPORTANT: the event loop must call `soft_clock.correct_timestamp()`
/// on every RX/TX software timestamp BEFORE passing it to
/// GptpSlavePort::receive_frame() / report_tx_timestamp(). This
/// converts raw CLOCK_REALTIME timestamps into the virtual clock
/// domain so the servo sees the effect of its own corrections.
///
/// @param raw_sock_fd  AF_PACKET socket with SO_TIMESTAMPING (SOFTWARE flags).
/// @param soft_clock   Shared virtual clock state. The returned lambdas
///                     write to it; the application reads from it.
/// @param ifname       Interface name for ethtool link speed query.
/// @param if_index     Interface index for sockaddr_ll.
/// @param src_mac      Local MAC address.
[[nodiscard]] auto make_linux_sw_clock_ops(
    int raw_sock_fd, SoftClock& soft_clock, std::string ifname, int if_index, ieee::Eui48 src_mac) -> GptpClockOps;

}  // namespace statusbar::gptp

#endif  // __linux__
