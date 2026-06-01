#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/net/net_address.hpp"
#include "statusbar/stun/stun_constants.hpp"
#include "statusbar/stun/stun_error.hpp"
#include "statusbar/stun/stun_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>

namespace statusbar::stun {

/// Parsed STUN message header (20 bytes on the wire).
struct MessageHeader
{
    Method method{Method::Binding};
    Class klass{Class::Request};
    uint16_t body_length{0};  ///< length in bytes of the attribute body, excluding the header
    TransactionId transaction_id{};
};

/// Encode a 20-byte STUN header into the start of `buf`. `body_length`
/// must be a multiple of 4. Returns HEADER_SIZE on success.
[[nodiscard]] auto encode_header(MessageHeader const& h, std::span<uint8_t> buf) -> std::error_code;

/// Decode a STUN header. Returns an error on bad magic, length not
/// multiple of 4, or buffer too small. Does not validate that the body
/// fits in `buf`.
[[nodiscard]] auto decode_header(std::span<uint8_t const> buf, MessageHeader& out) -> std::error_code;

/// Hard cap on attributes per message. Above this, the iterator returns
/// `StunError::AttributeTruncated` (re-using the existing protocol error
/// instead of inventing a new one — semantically the message is
/// "structurally invalid"). 16 is generous compared to the largest real
/// REGISTER message (<= 8 attributes) but small enough that a malicious
/// authenticated peer can't burn CPU on a million-attribute message.
inline constexpr size_t max_attributes_per_message = 16;

/// Iterator-style attribute walk. Yields a sequence of (type, value-span)
/// pairs over the attribute body. The caller passes the buffer slice that
/// covers the attribute body (i.e. starting at offset HEADER_SIZE and of
/// length body_length).
class AttributeIterator
{
  public:
    explicit AttributeIterator(std::span<uint8_t const> body) noexcept
        : body_{body}
    {}

    /// Read the next attribute. On success, `attr_type` and `value` are
    /// set and the iterator advances past the attribute (including its
    /// 4-byte alignment padding). Returns std::error_code{} when there
    /// are no more attributes; sets `done_out = true` in that case.
    /// Returns AttributeTruncated if the message contains more than
    /// `max_attributes_per_message` attributes.
    [[nodiscard]] auto next(uint16_t& attr_type, std::span<uint8_t const>& value, bool& done_out) -> std::error_code;

    /// Number of bytes consumed from the start of the body so far.
    [[nodiscard]] auto offset() const noexcept -> size_t { return cursor_; }

  private:
    std::span<uint8_t const> body_;
    size_t cursor_{0};
    size_t count_{0};
};

/// Append a TLV attribute to the buffer at the given offset and return
/// the new offset (with 4-byte padding). On insufficient space returns an
/// error and does not modify cursor.
[[nodiscard]] auto append_attribute(std::span<uint8_t> buf, size_t& cursor, uint16_t attr_type, std::span<uint8_t const> value)
    -> std::error_code;

/// Convenience: encode a XOR-MAPPED-ADDRESS attribute into `buf` at
/// `cursor`. The supplied `addr` must be IPv4 or IPv6. The transaction
/// id is needed for the IPv6 case.
[[nodiscard]] auto append_xor_mapped_address(
    std::span<uint8_t> buf,
    size_t& cursor,
    uint16_t attr_type,
    statusbar::net::SocketAddress const& addr,
    TransactionId const& txid) -> std::error_code;

/// Decode the bytes inside a XOR-MAPPED-ADDRESS attribute (i.e. the
/// value portion, NOT including the 4-byte TLV header) into a
/// SocketAddress.
[[nodiscard]] auto decode_xor_mapped_address(
    std::span<uint8_t const> value, TransactionId const& txid, statusbar::net::SocketAddress& out) -> std::error_code;

/// Encoded size of an XOR-MAPPED-ADDRESS attribute (TLV including padding)
/// for a given address family. Useful when sizing buffers up-front.
[[nodiscard]] auto xor_mapped_address_attribute_size(statusbar::net::SocketAddress const& addr) -> size_t;

/// Encode an ERROR-CODE attribute. `code` is the standard 100..699
/// integer; `reason` is a UTF-8 reason phrase, may be empty.
[[nodiscard]] auto append_error_code(std::span<uint8_t> buf, size_t& cursor, uint16_t code, std::string_view reason)
    -> std::error_code;

/// Decode an ERROR-CODE attribute body. Returns the integer code and
/// the reason phrase span (which points into the input).
[[nodiscard]] auto decode_error_code(std::span<uint8_t const> value, uint16_t& code, std::span<uint8_t const>& reason)
    -> std::error_code;

}  // namespace statusbar::stun
