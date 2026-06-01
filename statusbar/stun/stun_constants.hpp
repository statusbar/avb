#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include <cstddef>
#include <cstdint>

namespace statusbar::stun {

/// RFC 8489 §5 — fixed magic cookie that every STUN message after RFC 3489
/// carries in bytes [4..7]. Used by recipients to detect STUN multiplexed
/// over the same socket as another protocol.
inline constexpr uint32_t MAGIC_COOKIE = 0x2112'A442U;

inline constexpr size_t HEADER_SIZE = 20;
inline constexpr size_t TRANSACTION_ID_SIZE = 12;

/// XOR-MAPPED-ADDRESS XORs the port with the high 16 bits of the magic
/// cookie. Pre-extracted here to avoid recomputing.
inline constexpr uint16_t PORT_XOR_MASK = static_cast<uint16_t>(MAGIC_COOKIE >> 16);

inline constexpr size_t ATTRIBUTE_HEADER_SIZE = 4;
inline constexpr size_t max_message_length = 1500;

inline constexpr size_t SESSION_ID_SIZE = 16;
inline constexpr size_t EUI64_SIZE = 8;
inline constexpr size_t MIC_SIZE = 16;

/// Two leading bits of the message type field MUST be zero (RFC 8489 §5).
inline constexpr uint16_t LEADING_BITS_MASK = 0xC000U;

/// Bit positions of the message-type encoding. The wire field interleaves
/// class bits (C0, C1) and method bits inside a single 14-bit value.
inline constexpr uint16_t CLASS_C0_MASK = 0x0010U;  // bit 4 from LSB
inline constexpr uint16_t CLASS_C1_MASK = 0x0100U;  // bit 8 from LSB

/// STUN method numbers. Standard Binding (RFC 8489) plus our private
/// REGISTER. The 12-bit method space allows 0x000..0xFFF; we pick 0x010
/// which is in the IETF "experimental / private" range.
enum class Method : uint16_t
{
    Binding = 0x001,
    Register = 0x010,
};

enum class Class : uint8_t
{
    Request = 0,
    Indication = 1,
    SuccessResponse = 2,
    ErrorResponse = 3,
};

/// Standard STUN attribute types we care about. Comprehension-required
/// attributes live in 0x0000..0x7FFF; comprehension-optional in
/// 0x8000..0xFFFF. We add private types in the latter range so any
/// off-the-shelf STUN implementation can ignore them safely.
enum class AttributeType : uint16_t
{
    ErrorCode = 0x0009,
    XorMappedAddress = 0x0020,

    SessionId = 0x8030,
    ClientEui64 = 0x8031,
    PeerEui64 = 0x8032,
    PeerXorMappedAddress = 0x8033,
    SessionState = 0x8034,
    RefreshIntervalMs = 0x8035,
    Role = 0x8036,
    Mic = 0x8037,
};

[[nodiscard]] constexpr auto is_comprehension_required(uint16_t attr_type) noexcept -> bool
{
    return attr_type < 0x8000U;
}

/// XOR-MAPPED-ADDRESS family field, RFC 8489 §14.2.
enum class AddressFamily : uint8_t
{
    IPv4 = 0x01,
    IPv6 = 0x02,
};

/// Server-side per-session state. Carried in SESSION-STATE attribute on
/// success responses so the client knows whether to keep refreshing or
/// has been paired.
enum class SessionState : uint8_t
{
    Waiting = 0,
    Paired = 1,
    Expired = 2,
    Full = 3,
};

enum class Role : uint8_t
{
    Initiator = 0,
    Responder = 1,
};

/// Default UDP port for the STUN rendezvous server (STUN's IANA port).
inline constexpr uint16_t default_stun_port = 3478;

/// Default refresh interval the server suggests to clients. Chosen well
/// under the typical 30s consumer-NAT UDP mapping timeout. Used as the
/// keepalive cadence once a session is Paired.
inline constexpr uint32_t default_refresh_interval_ms = 15'000U;

/// Poll cadence the client re-REGISTERs at while still Waiting for its
/// peer to show up. The server is purely reactive — it never pushes an
/// unsolicited Paired notification — so a waiting client only discovers
/// pairing on its OWN next REGISTER. Polling at the 15s refresh cadence
/// means a staggered-start peer can wait up to a full refresh interval to
/// learn it is paired (and, under a finite rendezvous timeout, can land
/// right at the deadline and never enter its data phase). Keep this short
/// so discovery happens within ~1s of the peer arriving; it only applies
/// before pairing, so it is not the steady-state NAT keepalive rate.
inline constexpr uint32_t default_waiting_poll_interval_ms = 1'000U;

/// Default expiration: after this long without a refresh from a registered
/// client, the server drops the session entry.
inline constexpr uint32_t default_session_expiry_ms = 60'000U;

/// Initial RTO and retransmission budget for the client (RFC 8489 §6.2.1
/// gives Rc=7 / Rm=16 / RTO=500ms; we keep that shape but drive it from
/// our state machine rather than the spec's exact retry curve).
inline constexpr int64_t initial_rto_ms = 500;
inline constexpr int max_retransmits = 6;

}  // namespace statusbar::stun
