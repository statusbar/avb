// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_ntscf_v1.hpp"

#include "statusbar/buffer/span_utils.hpp"

namespace statusbar::avtp {

using statusbar::span_load;

auto ntscf_v1_parse_header(std::span<uint8_t const> const packet) noexcept -> std::optional<NtscfV1Pdu>
{
    if (packet.size() < NtscfV1Pdu::HEADER_LENGTH) {
        return std::nullopt;
    }

    NtscfV1Pdu pdu{};
    span_load(pdu, packet);

    if (!pdu.is_valid()) {
        return std::nullopt;
    }

    return pdu;
}

auto ntscf_v1_get_acf_payload(std::span<uint8_t const> const packet) noexcept -> std::span<uint8_t const>
{
    if (packet.size() <= NtscfV1Pdu::HEADER_LENGTH) {
        return {};
    }
    return packet.subspan(NtscfV1Pdu::HEADER_LENGTH);
}

}  // namespace statusbar::avtp
