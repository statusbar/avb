#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Client Pollable that drives a registration session against a STUN
/// server. Single transaction: one client, one session_id. Owns a UDP
/// socket. Feeds the client FSM with events derived from inbound
/// datagrams and time ticks.

#include "statusbar/crypto/aes_siv/aes128_siv.hpp"
#include "statusbar/ieee/ieee_ethernet.hpp"
#include "statusbar/net/net_address.hpp"
#include "statusbar/net/net_message_reactor.hpp"
#include "statusbar/sg14/inplace_function.h"
#include "statusbar/stun/stun_client_sm.hpp"
#include "statusbar/stun/stun_constants.hpp"
#include "statusbar/stun/stun_register.hpp"
#include "statusbar/stun/stun_types.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>

namespace statusbar::stun {

/// Callback fired exactly once when the client receives a Paired response
/// (i.e. learns its peer's reflexive address). The Pollable continues to
/// refresh after this point as a NAT keepalive, but additional pairings
/// do not re-fire the callback.
using OnPairedFn = statusbar::sg14::inplace_function<void(RegisterResponseSuccess const&), 128>;

/// Callback fired on terminal failure. After this fires, the Pollable
/// reports `finished()` true on the next tick.
using OnFailureFn = statusbar::sg14::inplace_function<void(std::error_code), 128>;

struct ClientConfig
{
    statusbar::net::SocketAddress server_address{};
    SessionId session_id{};
    statusbar::ieee::Eui64 client_eui64{};
    Role role{Role::Initiator};
    statusbar::crypto::Aes128SivKey shared_key{};
    OnPairedFn on_paired{};
    OnFailureFn on_failure{};
    int64_t initial_rto_ms{initial_rto_ms};
    int max_retransmits{max_retransmits};
    /// How often to re-REGISTER while Waiting for the peer (ms). Short by
    /// design so pairing is discovered within ~1s of the peer arriving;
    /// distinct from the server-dictated post-pairing keepalive refresh.
    uint32_t waiting_poll_interval_ms{default_waiting_poll_interval_ms};
    /// Print one stderr line per send / recv / refresh / retransmit.
    /// Off by default so unit tests stay quiet.
    bool verbose{false};
};

class StunClientPollable : public statusbar::net::Pollable
{
  public:
    /// Build a client bound to a UDP wildcard address (so the kernel
    /// picks a source port the NAT will track). Returns a populated
    /// unique_ptr, or nullptr + sets `ec_out` on socket error.
    static auto create(ClientConfig config, std::error_code& ec_out) -> std::unique_ptr<StunClientPollable>;

    ~StunClientPollable() override;

    StunClientPollable(StunClientPollable const&) = delete;
    auto operator=(StunClientPollable const&) -> StunClientPollable& = delete;

    [[nodiscard]] auto fd() const noexcept -> int override { return fd_; }
    void on_ready(int64_t now_ns) override;
    void tick(int64_t now_ns) override;
    [[nodiscard]] auto finished() const noexcept -> bool override { return finished_; }

    /// Current FSM state. Useful for tests.
    [[nodiscard]] auto state() const noexcept -> ClientDef::State { return sm_.current_state(); }

    /// Surrender ownership of the UDP socket. Returns the raw fd; the
    /// destructor will no longer close it. Used by perform_rendezvous so
    /// the same kernel-assigned source port (and its NAT mapping) can be
    /// reused by a downstream protocol like owlm.
    [[nodiscard]] auto release_fd() noexcept -> int
    {
        int const out = fd_;
        fd_ = -1;
        finished_ = true;
        return out;
    }

  private:
    StunClientPollable(ClientConfig config, int fd);

    void send_register_request(int64_t now_ns);
    void handle_datagram(std::span<uint8_t const> datagram, int64_t now_ns);
    void apply_action_flags();
    void handle_success(RegisterResponseSuccess const& success, int64_t now_ns);
    void handle_error(RegisterResponseError const& err);
    void emit_failure(std::error_code ec);

    [[nodiscard]] auto current_rto_ns() const noexcept -> int64_t;

    ClientConfig config_{};
    int fd_{-1};
    bool finished_{false};
    bool paired_callback_fired_{false};
    bool failure_callback_fired_{false};

    TransactionId outstanding_txid_{};
    int64_t request_sent_ns_{0};
    int64_t last_paired_response_ns_{0};

    ClientContext ctx_{};
    ClientStateMachine sm_{};

    std::array<uint8_t, max_message_length> rx_buf_{};
};

}  // namespace statusbar::stun
