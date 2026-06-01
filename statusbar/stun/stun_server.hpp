#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Server Pollable. Listens on a pair of UDP wildcard sockets (IPv4 + IPv6),
/// validates inbound REGISTER requests, manages a fixed-capacity session table,
/// and answers each request synchronously.

#include "statusbar/crypto/aes_siv/aes128_siv.hpp"
#include "statusbar/net/net_address.hpp"
#include "statusbar/net/net_message_reactor.hpp"
#include "statusbar/stun/stun_constants.hpp"
#include "statusbar/stun/stun_register.hpp"
#include "statusbar/stun/stun_server_sm.hpp"
#include "statusbar/stun/stun_types.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

namespace statusbar::stun {

inline constexpr size_t default_max_sessions = 64;

struct ServerConfig
{
    /// UDP port to listen on (both the IPv4 and IPv6 wildcards). 0 = let
    /// the OS choose an ephemeral port (used by tests).
    uint16_t bind_port{default_stun_port};
    statusbar::crypto::Aes128SivKey shared_key{};
    uint32_t refresh_interval_ms{default_refresh_interval_ms};
    uint32_t session_expiry_ms{default_session_expiry_ms};
    /// Print one stderr line per accepted/rejected REGISTER. Off by
    /// default so unit tests stay quiet.
    bool verbose{false};
};

namespace detail {

struct SessionTableEntry
{
    bool occupied{false};
    SessionId session_id{};
    ServerSessionContext ctx{};
    ServerSessionStateMachine sm{};
};

/// Outcome the server's request handler returns to the Pollable layer.
enum class ReplyKind : uint8_t
{
    SuccessWaiting,
    SuccessPaired,
    Error,
    Drop,
};

struct ReplyPlan
{
    ReplyKind kind{ReplyKind::Drop};
    RegisterResponseSuccess success{};
    RegisterResponseError error{};
    std::string_view error_reason{};
};

/// Implementation behind the templated StunServer. Lives in a
/// non-template class so the bulk of the code is compiled once; the
/// template only owns the fixed-size session array.
class ServerCore
{
  public:
    ServerCore(ServerConfig config, std::span<SessionTableEntry> sessions);

    [[nodiscard]] auto on_datagram(
        std::span<uint8_t const> datagram,
        statusbar::net::SocketAddress const& source,
        int64_t now_ns,
        std::span<uint8_t> reply_buf,
        size_t& reply_len_out) -> bool;

    void expire_old(int64_t now_ns);

    [[nodiscard]] auto config() const noexcept -> ServerConfig const& { return config_; }

    /// Number of sessions currently occupied (used by tests).
    [[nodiscard]] auto live_session_count() const noexcept -> size_t;

  private:
    [[nodiscard]] auto find_session(SessionId const& sid) -> SessionTableEntry*;
    [[nodiscard]] auto allocate_session(SessionId const& sid) -> SessionTableEntry*;

    [[nodiscard]] auto plan_reply(RegisterRequest const& req, statusbar::net::SocketAddress const& source, int64_t now_ns)
        -> ReplyPlan;

    [[nodiscard]] auto build_success(
        SessionTableEntry const& entry,
        ServerSessionEntry const& self,
        ServerSessionEntry const* peer,
        TransactionId const& txid) const -> ReplyPlan;

    [[nodiscard]] auto encode_plan(ReplyPlan const& plan, std::span<uint8_t> reply_buf, size_t& reply_len_out) const -> bool;

    ServerConfig config_;
    std::span<SessionTableEntry> sessions_;
};

}  // namespace detail

/// One UDP socket of the dual-stack server. Thin Pollable: recvfrom on
/// its own fd, hand the datagram to the shared ServerCore, sendto the
/// reply on the same fd. Several of these share one ServerCore.
class StunSocketPollable final : public statusbar::net::Pollable
{
  public:
    StunSocketPollable(int fd, detail::ServerCore& core, bool const& finished, bool owns_expiry_tick) noexcept;
    ~StunSocketPollable() override;

    StunSocketPollable(StunSocketPollable const&) = delete;
    auto operator=(StunSocketPollable const&) -> StunSocketPollable& = delete;
    StunSocketPollable(StunSocketPollable&&) = delete;
    auto operator=(StunSocketPollable&&) -> StunSocketPollable& = delete;

    [[nodiscard]] auto fd() const noexcept -> int override { return fd_; }
    void on_ready(int64_t now_ns) override;
    void tick(int64_t now_ns) override;
    [[nodiscard]] auto finished() const noexcept -> bool override { return finished_; }

  private:
    int fd_{-1};
    detail::ServerCore& core_;
    bool const& finished_;
    bool owns_expiry_tick_{false};
    std::array<uint8_t, max_message_length> rx_buf_{};
    std::array<uint8_t, max_message_length> tx_buf_{};
};

/// Dual-stack STUN rendezvous server. Owns the session table and the
/// ServerCore; opens an IPv4 and an IPv6 wildcard UDP socket. Hand the
/// pollables (via take_pollables) to a MessageReactor; this object
/// must outlive the reactor run.
template <size_t MaxSessions = default_max_sessions>
class StunServer
{
  public:
    static auto create(ServerConfig config, std::error_code& ec_out) -> std::unique_ptr<StunServer>;

    // Non-movable: the StunSocketPollables hold references into this
    // object's core_ / finished_ members, so its address must be stable.
    StunServer(StunServer const&) = delete;
    auto operator=(StunServer const&) -> StunServer& = delete;
    StunServer(StunServer&&) = delete;
    auto operator=(StunServer&&) -> StunServer& = delete;

    /// The pollables for the opened sockets (one or two). Move them
    /// into a MessageReactor. Call once.
    [[nodiscard]] auto take_pollables() -> std::vector<std::unique_ptr<statusbar::net::Pollable>>;

    /// The actual bound port (resolved when bind_port was 0).
    [[nodiscard]] auto port() const noexcept -> uint16_t { return port_; }

    [[nodiscard]] auto live_session_count() const noexcept -> size_t { return core_.live_session_count(); }

    /// Stop all sockets on the next reactor pass (tests/tools).
    void request_stop() noexcept { finished_ = true; }

  private:
    StunServer(ServerConfig config, uint16_t port);

    bool finished_{false};
    uint16_t port_{0};
    std::array<detail::SessionTableEntry, MaxSessions> sessions_{};
    detail::ServerCore core_;
    std::vector<std::unique_ptr<StunSocketPollable>> pollables_{};
};

extern template class StunServer<default_max_sessions>;

}  // namespace statusbar::stun
