// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_crf_v1.hpp"

#include "statusbar/buffer/span_utils.hpp"

namespace statusbar::avtp {

using statusbar::span_load;

auto crf_v1_parse_header(std::span<uint8_t const> const packet) noexcept -> std::optional<CrfV1Pdu>
{
    if (packet.size() < CrfV1Pdu::HEADER_LENGTH) {
        return std::nullopt;
    }

    CrfV1Pdu pdu{};
    span_load(pdu, packet);

    if (!pdu.is_valid()) {
        return std::nullopt;
    }

    return pdu;
}

auto crf_v1_get_timestamp_data(std::span<uint8_t const> const packet) noexcept -> std::span<uint8_t const>
{
    if (packet.size() <= CrfV1Pdu::HEADER_LENGTH) {
        return {};
    }
    return packet.subspan(CrfV1Pdu::HEADER_LENGTH);
}

}  // namespace statusbar::avtp
