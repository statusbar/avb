#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Synchronous rendezvous helper. Drives a STUN client to completion and
/// hands the resulting UDP socket back to the caller along with the peer's
/// reflexive address. Designed for use as a one-shot prelude before
/// downstream UDP protocols like owlm that need to reuse the same kernel
/// source port (and therefore the same NAT mapping) the STUN exchange
/// established.

#include "statusbar/crypto/aes_siv/aes128_siv.hpp"
#include "statusbar/ieee/ieee_ethernet.hpp"
#include "statusbar/net/net_address.hpp"
#include "statusbar/net/net_socket.hpp"
#include "statusbar/stun/stun_constants.hpp"
#include "statusbar/stun/stun_types.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <system_error>

namespace statusbar::stun {

struct RendezvousConfig
{
    statusbar::net::SocketAddress server_address{};
    SessionId session_id{};
    statusbar::ieee::Eui64 client_eui64{};
    Role role{Role::Initiator};
    statusbar::crypto::Aes128SivKey shared_key{};

    /// 0 = let the kernel pick. Set when the caller needs a specific
    /// source port (e.g., to align with another protocol's binding).
    uint16_t local_port{0};

    /// Empty = no SO_BINDTODEVICE. Set to a network interface name to
    /// pin the socket to a specific NIC (Linux only; ignored elsewhere).
    std::string local_interface{};

    /// Hard ceiling on how long perform_rendezvous waits before
    /// returning a TimedOut error. The default is generous enough to
    /// survive the second peer being a few seconds slow to start.
    int64_t timeout_ms{60'000};

    /// Print one stderr line per send/recv if true. Wired through to the
    /// underlying StunClientPollable's verbose flag.
    bool verbose{false};
};

struct RendezvousResult
{
    /// Owns the UDP socket. The caller takes ownership and is
    /// responsible for closing it.
    statusbar::net::FileDescriptor socket{};

    /// Local bound address of `socket` after the kernel has assigned a
    /// port (or just the explicitly-requested port if local_port != 0).
    /// Useful for tools that want to log "bound on X:Y".
    statusbar::net::SocketAddress local_address{};

    /// The reflexive address the server saw for this client (after
    /// passing through whatever NAT is between us and the server).
    statusbar::net::SocketAddress my_reflexive_address{};

    /// The reflexive address the server saw for our peer. This is the
    /// destination owlm / etc. should send to.
    statusbar::net::SocketAddress peer_reflexive_address{};

    /// The peer's EUI-64 as it registered with the server.
    statusbar::ieee::Eui64 peer_eui64{};
};

/// Run a rendezvous session to completion. Blocks (driving an internal
/// poll loop) until either the server pairs us with our peer or the
/// timeout expires. Returns the populated result on success or an
/// error_code on any failure (socket creation, bind, decode, MIC,
/// timeout, etc).
[[nodiscard]] auto perform_rendezvous(RendezvousConfig const& cfg) -> std::expected<RendezvousResult, std::error_code>;

}  // namespace statusbar::stun
