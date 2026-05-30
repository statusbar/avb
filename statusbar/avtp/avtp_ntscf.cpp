// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_ntscf.hpp"

#include "statusbar/buffer/span_utils.hpp"

namespace statusbar::avtp {

using statusbar::span_load;

auto ntscf_parse_header(std::span<uint8_t const> const packet) noexcept -> std::optional<NtscfPdu>
{
    if (packet.size() < NtscfPdu::HEADER_LENGTH) {
        return std::nullopt;
    }

    NtscfPdu pdu{};
    span_load(pdu, packet);

    if (!pdu.is_valid()) {
        return std::nullopt;
    }

    return pdu;
}

auto ntscf_get_acf_payload(std::span<uint8_t const> const packet) noexcept -> std::span<uint8_t const>
{
    if (packet.size() <= NtscfPdu::HEADER_LENGTH) {
        return {};
    }
    return packet.subspan(NtscfPdu::HEADER_LENGTH);
}

}  // namespace statusbar::avtp
