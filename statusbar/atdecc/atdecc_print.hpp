#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/atdecc/atdecc_acmp.hpp"
#include "statusbar/atdecc/atdecc_acmp_format.hpp"
#include "statusbar/atdecc/atdecc_adp.hpp"
#include "statusbar/atdecc/atdecc_adp_format.hpp"
#include "statusbar/atdecc/atdecc_aecp.hpp"
#include "statusbar/atdecc/atdecc_aecp_aem.hpp"
#include "statusbar/atdecc/atdecc_aecp_aem_format.hpp"
#include "statusbar/atdecc/atdecc_aecp_format.hpp"
#include "statusbar/atdecc/atdecc_aem_format.hpp"
#include "statusbar/avtp/avtp_format.hpp"
#include "statusbar/ieee/ieee.hpp"

#include <cstdint>
#include <format>
#include <iterator>
#include <span>
#include <string>

namespace statusbar::atdecc {

namespace detail {

/// Format a hex dump to an output iterator
/// @param out Output iterator to write formatted text to
/// @param data Raw bytes to dump as hex
template <typename OutputIt>
auto format_hex_dump(OutputIt out, std::span<uint8_t const> data) -> OutputIt
{
    for (auto const octet_value : data) {
        out = std::format_to(out, "{:02x} ", octet_value);
    }
    out = std::format_to(out, "\n");
    return out;
}

}  // namespace detail

/// Format an ATDECC payload (AECP, ACMP, ADP, or MAAP) to an output iterator
/// @param out Output iterator to write formatted text to
/// @param payload Raw AVTP payload bytes starting at the subtype byte
template <typename OutputIt>
auto format_atdecc(OutputIt out, std::span<uint8_t const> payload) -> OutputIt
{
    if (payload.empty()) {
        return std::format_to(out, "  AVTP payload empty\n");
    }

    // Extract subtype from first byte
    uint8_t const subtype = payload[0];

    switch (subtype) {
        case avtp::AvtpSubtype::aecp: {
            if (payload.size() < AecpDuCommon::LENGTH) {
                out = std::format_to(out, "  ATDECC AECP truncated ({} bytes, need {})\n", payload.size(), AecpDuCommon::LENGTH);
                return detail::format_hex_dump(out, payload);
            }

            // Check if this is an AEM message (command or response)
            // Message type is in bits [3:0] of byte 1
            uint8_t const message_type = payload[1] & 0x0F;
            bool const is_aem = (message_type == AECP_MESSAGE_TYPE_AEM_COMMAND || message_type == AECP_MESSAGE_TYPE_AEM_RESPONSE);

            if (is_aem) {
                // AEM messages have an extended header (24 bytes) with command_type field
                if (payload.size() < AemDu::LENGTH) {
                    out = std::format_to(out, "  ATDECC AEM truncated ({} bytes, need {})\n", payload.size(), AemDu::LENGTH);
                    return detail::format_hex_dump(out, payload);
                }

                AemDu aem;
                (void)load_unchecked(payload, &aem);
                out = std::format_to(out, "  ");
                out = format_to(out, aem);
                out = std::format_to(out, "\n");

                // Parse and format the AEM command/response payload
                auto remaining = payload.subspan(AemDu::LENGTH);
                out = format_aem_payload(out, aem, remaining);
            } else {
                // Non-AEM AECP message (Address Access, Vendor Unique, etc.)
                AecpDuCommon aecp;
                (void)load_unchecked(payload, &aecp);
                out = std::format_to(out, "  ");
                out = format_to(out, aecp);
                out = std::format_to(out, "\n");

                // Print remaining payload after common header
                auto remaining = payload.subspan(AecpDuCommon::LENGTH);
                if (!remaining.empty()) {
                    out = std::format_to(out, "        payload ({} bytes): ", remaining.size());
                    out = detail::format_hex_dump(out, remaining);
                }
            }
            break;
        }
        case avtp::AvtpSubtype::acmp:
            if (payload.size() < AcmpDu::LENGTH) {
                out = std::format_to(out, "  ATDECC ACMP truncated ({} bytes, need {})\n", payload.size(), AcmpDu::LENGTH);
                return detail::format_hex_dump(out, payload);
            }
            {
                AcmpDu acmp;
                (void)load_unchecked(payload, &acmp);
                out = std::format_to(out, "  ");
                out = format_to(out, acmp);
                out = std::format_to(out, "\n");
            }
            break;
        case avtp::AvtpSubtype::adp:
            if (payload.size() < AdpDu::LENGTH) {
                out = std::format_to(out, "  ATDECC ADP truncated ({} bytes, need {})\n", payload.size(), AdpDu::LENGTH);
                return detail::format_hex_dump(out, payload);
            }
            {
                AdpDu adp;
                (void)load_unchecked(payload, &adp);
                out = std::format_to(out, "  ");
                out = format_to(out, adp);
                out = std::format_to(out, "\n");
            }
            break;
        case avtp::AvtpSubtype::maap:
            if (payload.size() < avtp::MaapDu::LENGTH) {
                out = std::format_to(out, "  MAAP truncated ({} bytes, need {})\n", payload.size(), avtp::MaapDu::LENGTH);
                return detail::format_hex_dump(out, payload);
            }
            {
                avtp::MaapDu maap;
                (void)avtp::load_unchecked(payload, &maap);
                out = std::format_to(out, "  ");
                out = avtp::format_to(out, maap);
                out = std::format_to(out, "\n");
            }
            break;
        default:
            out = std::format_to(out, "  AVTP subtype={:#04x} ({} bytes): ", subtype, payload.size());
            out = detail::format_hex_dump(out, payload);
            break;
    }

    return out;
}

}  // namespace statusbar::atdecc
