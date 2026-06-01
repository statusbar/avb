#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Stateless UDP reflector. Validates each received datagram via the
/// codec's `validate_for_reflect` and bounces accepted bytes back to
/// the source verbatim. Used to measure round-trip latency from a
/// peer that sends through `udptun::send_packet` to a remote endpoint
/// running this loop.

#include <cstdint>
#include <ostream>
#include <print>
#include <string>
#include <string_view>

namespace statusbar::udptun {

/// Cumulative counters maintained by `run_reflect`.
struct ReflectStats
{
    uint64_t reflected{0};
    uint64_t dropped_invalid{0};
    /// Datagrams larger than the drain buffer, reported by MSG_TRUNC.
    /// These cannot be reflected safely (truncated bytes wouldn't form a
    /// valid codec packet) and are skipped before validate_for_reflect.
    uint64_t rx_truncated{0};
    /// sendto() echoes that returned <0 (e.g. ENOBUFS, queue full). The
    /// underlying datagram was validated and counted into `reflected`,
    /// but the actual echo never made it onto the wire.
    uint64_t tx_failed{0};
};

/// Configuration for `run_reflect`. The label is printed in the
/// startup banner so the operator can tell which tool is bound to the
/// reflect port.
struct ReflectConfig
{
    uint16_t local_port{};
    std::string interface{};  ///< empty = wildcard bind, no SO_BINDTODEVICE
    bool ipv6{false};
    int dscp{-1};                       ///< -1 disables DSCP
    std::string_view label{"reflect"};  ///< stderr banner prefix
};

/// One-line summary printed when the reflector exits.
inline void print_reflect_summary(std::ostream& out, ReflectStats const& s)
{
    std::println(
        out,
        "reflect: reflected={} dropped_invalid={} rx_truncated={} tx_failed={}",
        s.reflected,
        s.dropped_invalid,
        s.rx_truncated,
        s.tx_failed);
}

}  // namespace statusbar::udptun

#include "statusbar/itc/itc_stop_token.hpp"
#include "statusbar/net/net.hpp"
#include "statusbar/udptun/udptun_codec_concept.hpp"
#include "statusbar/udptun/udptun_session_helpers.hpp"

#include <poll.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <iostream>
#include <span>

#include <sys/socket.h>
#include <sys/types.h>

namespace statusbar::udptun {

/// Drain the UDP socket nonblocking-recvfrom loop. Each datagram is
/// validated via `codec.validate_for_reflect` and either echoed back
/// to the source (incrementing `stats.reflected`) or counted as
/// `stats.dropped_invalid`. The buffer is sized for jumbo Ethernet;
/// MSG_TRUNC lets us detect peers sending datagrams even larger than
/// that — those go to `stats.rx_truncated` and are skipped before
/// validation. sendto failures (queue full, etc.) are counted into
/// `stats.tx_failed`.
template <Codec C>
void drain_reflect_rx(C const& codec, int udp_fd, ReflectStats& stats) noexcept
{
    std::array<uint8_t, 9216> buf{};
    while (true) {
        net::SocketAddress src{};
        src.reset_length();
        ssize_t const n = ::recvfrom(udp_fd, buf.data(), buf.size(), MSG_DONTWAIT | MSG_TRUNC, src.sockaddr(), src.length_ptr());
        if (n <= 0) {
            return;
        }
        auto const datagram_bytes = static_cast<size_t>(n);
        if (datagram_bytes > buf.size()) {
            ++stats.rx_truncated;
            continue;
        }
        auto const span = std::span<uint8_t const>(buf.data(), datagram_bytes);
        if (!codec.validate_for_reflect(span)) {
            ++stats.dropped_invalid;
            continue;
        }
        ssize_t const sent = ::sendto(udp_fd, buf.data(), datagram_bytes, MSG_DONTWAIT, src.sockaddr(), src.length());
        if (sent < 0) {
            ++stats.tx_failed;
            continue;
        }
        ++stats.reflected;
    }
}

/// Bind a UDP socket per `cfg`, run the validate-then-echo loop until
/// SIGINT/SIGTERM, then print the summary. Returns 0 on clean exit, 1
/// on socket setup failure.
template <Codec C>
[[nodiscard]] auto run_reflect(C const& codec, ReflectConfig const& cfg) -> int
{
    auto local_addr = cfg.ipv6 ? net::SocketAddress::ipv6_any(cfg.local_port) : net::SocketAddress::ipv4_any(cfg.local_port);
    auto udp_res = net::create_udp_socket(local_addr, /*do_bind=*/true, cfg.dscp);
    if (!udp_res) {
        std::println(stderr, "create_udp_socket failed");
        return 1;
    }
    net::FileDescriptor udp{std::move(*udp_res)};
    if (auto st = net::set_nonblocking(udp.get()); !st) {
        std::println(stderr, "set_nonblocking failed");
        return 1;
    }
    bind_to_interface(udp.get(), cfg.ipv6 ? AF_INET6 : AF_INET, cfg.interface);

    std::println(
        stderr,
        "{}: bound on {} port {} (ipv6={})",
        cfg.label,
        cfg.interface.empty() ? "*" : cfg.interface.c_str(),
        cfg.local_port,
        cfg.ipv6);

    ReflectStats stats{};
    auto& stop = statusbar::itc::install_stop_signal();

    while (!stop.stop_requested()) {
        pollfd pfd{.fd = udp.get(), .events = POLLIN, .revents = 0};
        int const rv = ::poll(&pfd, 1, /*timeout_ms*/ 100);
        if (rv < 0 && errno == EINTR) {
            continue;
        }
        if ((pfd.revents & POLLIN) != 0) {
            drain_reflect_rx(codec, udp.get(), stats);
        }
    }

    print_reflect_summary(std::cout, stats);
    return 0;
}

}  // namespace statusbar::udptun
