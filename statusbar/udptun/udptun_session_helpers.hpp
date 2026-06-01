#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Socket / multicast / poll-fd helpers used by the udptun session
/// loop. Codec-agnostic; live in their own header so the templated
/// session code (udptun_session.hpp) doesn't drag the syscall surface
/// into every consumer.
///
/// Periodic timing in the session uses udptun::DeadlineTimer (computed
/// deadlines + poll() timeouts), not kernel timerfds — the loop is now
/// portable across Linux / macOS / BSD without needing per-OS timer fds.

#include "statusbar/net/net.hpp"

#include <poll.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#if defined(__linux__)
namespace statusbar::gptp {
class SlaveSession;
}
#endif

namespace statusbar::udptun {

/// Apply a per-OS bind-to-interface sockopt to `fd` for `iface`. Empty
/// `iface` is a no-op. Linux uses SO_BINDTODEVICE; macOS / BSD use
/// IP_BOUND_IF / IPV6_BOUND_IF (chosen by `family`). Logs a warning to
/// stderr on failure but does not throw.
void bind_to_interface(int fd, int family, std::string const& iface);

/// Backwards-compatible alias for `bind_to_interface(fd, AF_INET, iface)`.
inline void bind_to_device(int fd, std::string const& iface)
{
    bind_to_interface(fd, AF_INET, iface);
}

/// True iff `addr` is in the IPv4 224.0.0.0/4 multicast range. AF_INET6
/// always returns false (IPv6 multicast not yet supported by these
/// helpers).
[[nodiscard]] auto is_ipv4_multicast(net::SocketAddress const& addr) -> bool;

/// Join an IPv4 multicast group on `fd` and configure outgoing TTL +
/// loopback-suppression. `iface` selects the interface for the join;
/// if empty the kernel chooses. Returns true on success.
[[nodiscard]] auto configure_multicast(int fd, std::string const& iface, net::SocketAddress const& group, int ttl) -> bool;

#if defined(__linux__)
/// Build the pollfd array for a session loop iteration. Order is:
/// gPTP session fds (if any), then udp_fd. Returns the number of fds
/// populated. Capped at 2 (gptp + udp); the templated session loop
/// drives its periodic ticks via DeadlineTimer rather than additional
/// poll fds.
[[nodiscard]] auto build_pollfds(std::optional<gptp::SlaveSession> const& session, int udp_fd, std::array<pollfd, 2>& fds) noexcept
    -> size_t;
#endif

/// gPTP-less variant for non-Linux builds and Linux --no-gptp runs.
/// Always populates index 0 with udp_fd and returns 1.
[[nodiscard]] auto build_pollfds_udp_only(int udp_fd, std::array<pollfd, 2>& fds) noexcept -> size_t;

/// Choose a grace window for "missing" packet accounting based on the
/// observed max RTT. Two RTTs of headroom catches normal jitter while
/// a 100 ms floor keeps the live counter stable on the very first
/// report tick before any RTT samples have been recorded.
[[nodiscard]] inline auto rtt_grace_ns(int64_t observed_max_rtt_ns) noexcept -> int64_t
{
    constexpr int64_t floor_ns = 100'000'000;  // 100 ms
    int64_t const doubled = (observed_max_rtt_ns > 0) ? 2 * observed_max_rtt_ns : 0;
    return (doubled > floor_ns) ? doubled : floor_ns;
}

}  // namespace statusbar::udptun
