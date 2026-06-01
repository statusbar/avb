// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// clang-format off
// Include order differs between clang-19 and clang-22 here — wrap-off
// pins it so both versions produce the same layout.
#include "statusbar/owlm/owlm_packet.hpp"
#include "statusbar/status/statusbar_assert.hpp"

#include <cstring>
// clang-format on

namespace statusbar::owlm {

namespace {

void write_u32_be(uint8_t* p, uint32_t v) noexcept
{
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}

void write_u16_be(uint8_t* p, uint16_t v) noexcept
{
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}

void write_i64_be(uint8_t* p, int64_t v) noexcept
{
    auto u = static_cast<uint64_t>(v);
    for (int i = 7; i >= 0; --i) {
        p[i] = static_cast<uint8_t>(u & 0xFF);
        u >>= 8;
    }
}

[[nodiscard]] auto read_u32_be(uint8_t const* p) noexcept -> uint32_t
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) |
        static_cast<uint32_t>(p[3]);
}

[[nodiscard]] auto read_u16_be(uint8_t const* p) noexcept -> uint16_t
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}

[[nodiscard]] auto read_i64_be(uint8_t const* p) noexcept -> int64_t
{
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) {
        u = (u << 8) | static_cast<uint8_t>(p[i]);
    }
    return static_cast<int64_t>(u);
}

}  // namespace

auto encode_owlm_packet(OwlmPacket const& p, std::span<uint8_t> buf) -> size_t
{
    auto const buf_size = buf.size();
    STATUSBAR_ASSERT(buf_size >= OwlmPacket::HEADER_SIZE);
    auto* d = buf.data();
    write_u32_be(d + 0, OwlmPacket::MAGIC);
    write_u16_be(d + 4, OwlmPacket::VERSION);
    write_u16_be(d + 6, 0);

    // Eui64 stores bytes already in network byte order via IeeeOrderedUInt.
    auto const eui_span = p.sender_eui64.span();
    for (size_t i = 0; i < 8; ++i) {
        d[8 + i] = eui_span[i];
    }

    write_u32_be(d + 16, p.sequence);
    write_i64_be(d + 20, p.tx_gptp_ns);
    write_u32_be(d + 28, p.tx_interval_us);
    return OwlmPacket::HEADER_SIZE;
}

auto decode_owlm_packet(std::span<uint8_t const> buf, OwlmPacket& out) -> std::error_code
{
    if (buf.size() < OwlmPacket::HEADER_SIZE) {
        return make_error_code(OwlmError::DatagramTooShort);
    }
    auto const* d = buf.data();

    if (read_u32_be(d + 0) != OwlmPacket::MAGIC) {
        return make_error_code(OwlmError::InvalidMagic);
    }
    if (read_u16_be(d + 4) != OwlmPacket::VERSION) {
        return make_error_code(OwlmError::UnsupportedVersion);
    }
    if (read_u16_be(d + 6) != 0) {
        return make_error_code(OwlmError::ReservedFlagsSet);
    }

    auto eui_span = out.sender_eui64.span();
    for (size_t i = 0; i < 8; ++i) {
        eui_span[i] = d[8 + i];
    }

    out.sequence = read_u32_be(d + 16);
    out.tx_gptp_ns = read_i64_be(d + 20);
    out.tx_interval_us = read_u32_be(d + 28);

    if (out.tx_interval_us == 0 || out.tx_interval_us > OwlmPacket::max_interval_us) {
        return make_error_code(OwlmError::InvalidInterval);
    }
    if (out.tx_gptp_ns <= 0) {
        return make_error_code(OwlmError::SenderNotSynced);
    }
    return {};
}

}  // namespace statusbar::owlm
