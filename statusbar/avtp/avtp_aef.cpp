// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_aef.hpp"

#include "statusbar/buffer/span_utils.hpp"

namespace statusbar::avtp {

using statusbar::span_load;

auto aef_enc_mode_name(uint8_t const enc) noexcept -> char const*
{
    switch (enc) {
        case 0x00:
            return "AES-SIV";
        case 0x01:
            return "AES-GCM-SIV";
        default:
            return "Reserved";
    }
}

auto aef_continuous_parse_header(std::span<uint8_t const> const packet) noexcept -> std::optional<AefContinuousPdu>
{
    if (packet.size() < AefContinuousPdu::HEADER_LENGTH) {
        return std::nullopt;
    }

    AefContinuousPdu pdu{};
    span_load(pdu, packet);

    if (!pdu.is_valid()) {
        return std::nullopt;
    }

    return pdu;
}

auto aef_discrete_parse_header(std::span<uint8_t const> const packet) noexcept -> std::optional<AefDiscretePdu>
{
    if (packet.size() < AefDiscretePdu::HEADER_LENGTH) {
        return std::nullopt;
    }

    AefDiscretePdu pdu{};
    span_load(pdu, packet);

    if (!pdu.is_valid()) {
        return std::nullopt;
    }

    return pdu;
}

auto aef_continuous_get_encrypted_payload(std::span<uint8_t const> const packet) noexcept -> std::span<uint8_t const>
{
    if (packet.size() <= AefContinuousPdu::HEADER_LENGTH) {
        return {};
    }
    return packet.subspan(AefContinuousPdu::HEADER_LENGTH);
}

auto aef_discrete_get_encrypted_payload(std::span<uint8_t const> const packet) noexcept -> std::span<uint8_t const>
{
    if (packet.size() <= AefDiscretePdu::HEADER_LENGTH) {
        return {};
    }
    return packet.subspan(AefDiscretePdu::HEADER_LENGTH);
}

}  // namespace statusbar::avtp
