// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/stun/stun_rendezvous.hpp"

#include "statusbar/itc/itc_stop_token.hpp"
#include "statusbar/net/net_message_reactor.hpp"
#include "statusbar/net/net_socket.hpp"
#include "statusbar/stun/stun_client.hpp"
#include "statusbar/stun/stun_error.hpp"

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#if defined(__linux__)
#    include <net/if.h>
#endif

namespace statusbar::stun {

namespace {

/// Read back the kernel-assigned bind address from an open UDP fd.
[[nodiscard]] auto sockname_of(int fd) -> statusbar::net::SocketAddress
{
    statusbar::net::SocketAddress out{};
    out.reset_length();
    if (::getsockname(fd, out.sockaddr(), out.length_ptr()) != 0) {
        return statusbar::net::SocketAddress{};
    }
    return out;
}

// bind-to-interface lives in net::bind_to_interface (header-only,
// per-OS branches). This file uses it directly so Linux + macOS + BSD
// share one implementation without dragging udptun into the dep graph.

}  // namespace

auto perform_rendezvous(RendezvousConfig const& cfg) -> std::expected<RendezvousResult, std::error_code>
{
    if (!cfg.server_address.valid()) {
        return std::unexpected(make_error_code(StunError::NoServerAddress));
    }

    bool paired = false;
    bool failed = false;
    std::error_code failure_ec{};
    std::optional<RegisterResponseSuccess> paired_response{};

    ClientConfig client_cfg{
        .server_address = cfg.server_address,
        .session_id = cfg.session_id,
        .client_eui64 = cfg.client_eui64,
        .role = cfg.role,
        .shared_key = cfg.shared_key,
        .on_paired =
            [&](RegisterResponseSuccess const& r) {
                paired = true;
                paired_response = r;
            },
        .on_failure =
            [&](std::error_code ec) {
                failed = true;
                failure_ec = ec;
            },
        .verbose = cfg.verbose,
    };

    std::error_code create_ec{};
    auto client = StunClientPollable::create(std::move(client_cfg), create_ec);
    if (!client) {
        return std::unexpected(create_ec);
    }

    // Apply optional bind-to-device + explicit local port. If the caller
    // requested a specific local_port, we have to rebind the fd because
    // StunClientPollable::create only opened a fresh unbound socket.
    int const fd = client->fd();
    int const family = cfg.server_address.family();
    if (!cfg.local_interface.empty() && !statusbar::net::bind_to_interface(fd, family, cfg.local_interface)) {
        return std::unexpected(make_error_code(StunError::SocketBindFailed));
    }
    if (cfg.local_port != 0) {
        auto const family = cfg.server_address.family();
        auto local = (family == AF_INET6) ? statusbar::net::SocketAddress::ipv6_any(cfg.local_port)
                                          : statusbar::net::SocketAddress::ipv4_any(cfg.local_port);
        if (::bind(fd, local.sockaddr(), local.length()) != 0) {
            return std::unexpected(make_error_code(StunError::SocketBindFailed));
        }
    }

    // Drive the poll loop until paired, failed, or timed out. We use a
    // dedicated reactor + StopToken so this helper does not interfere
    // with any reactor the caller is running.
    statusbar::itc::StopToken stop;
    statusbar::net::MessageReactor reactor{stop, statusbar::net::monotonic_ns, 50};
    StunClientPollable* client_raw = client.get();
    reactor.add(std::move(client));

    auto const start = std::chrono::steady_clock::now();
    auto const deadline = start + std::chrono::milliseconds(cfg.timeout_ms);

    while (!paired && !failed && std::chrono::steady_clock::now() < deadline) {
        reactor.poll_once(50);
    }

    if (failed) {
        return std::unexpected(failure_ec);
    }
    if (!paired || !paired_response.has_value()) {
        return std::unexpected(make_error_code(StunError::SessionExpired));
    }
    if (!paired_response->peer_xor_mapped_address.has_value() || !paired_response->peer_eui64.has_value()) {
        return std::unexpected(make_error_code(StunError::MissingRequiredAttribute));
    }

    int const released_fd = client_raw->release_fd();
    RendezvousResult result{
        .socket = statusbar::net::FileDescriptor{released_fd},
        .local_address = sockname_of(released_fd),
        .my_reflexive_address = paired_response->xor_mapped_address,
        .peer_reflexive_address = *paired_response->peer_xor_mapped_address,
        .peer_eui64 = *paired_response->peer_eui64,
    };
    return result;
}

}  // namespace statusbar::stun
