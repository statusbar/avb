// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_aaf_v1.hpp"

#include "statusbar/buffer/span_utils.hpp"

namespace statusbar::avtp {

using statusbar::span_load;

auto aaf_v1_parse_header(std::span<uint8_t const> const packet) noexcept -> std::optional<AafV1Pdu>
{
    if (packet.size() < AafV1Pdu::HEADER_LENGTH) {
        return std::nullopt;
    }

    AafV1Pdu pdu{};
    span_load(pdu, packet);

    if (!pdu.is_valid()) {
        return std::nullopt;
    }

    return pdu;
}

auto aaf_v1_get_audio_payload(std::span<uint8_t const> const packet) noexcept -> std::span<uint8_t const>
{
    if (packet.size() <= AafV1Pdu::HEADER_LENGTH) {
        return {};
    }
    return packet.subspan(AafV1Pdu::HEADER_LENGTH);
}

}  // namespace statusbar::avtp
