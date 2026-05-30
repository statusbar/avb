#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/srp/srp_mrp.hpp"
#include "statusbar/srp/srp_msrp.hpp"

#include <cstdint>
#include <format>
#include <span>

namespace statusbar::srp::mrp {

/// Format MRP events (ThreePacked) and optional MSRP Listener declarations (FourPacked)
/// to an output iterator
template <typename OutputIt>
auto format_mrp_events(OutputIt out, uint16_t number_of_values, std::span<uint8_t const> events_data, bool has_declarations)
    -> OutputIt
{
    size_t const num_event_octets = threepacked_octet_count(number_of_values);

    if (events_data.size() < num_event_octets) {
        return std::format_to(out, "      Events truncated\n");
    }

    out = std::format_to(out, "      Events: ");
    size_t event_index = 0;
    for (size_t i = 0; i < num_event_octets && event_index < number_of_values; ++i) {
        auto const unpacked = unpack3_events(events_data[i]);
        if (event_index < number_of_values) {
            out = std::format_to(out, "{}", attribute_event_name(unpacked.first));
            ++event_index;
        }
        if (event_index < number_of_values) {
            out = std::format_to(out, ", {}", attribute_event_name(unpacked.second));
            ++event_index;
        }
        if (event_index < number_of_values) {
            out = std::format_to(out, ", {}", attribute_event_name(unpacked.third));
            ++event_index;
        }
    }
    out = std::format_to(out, "\n");

    // For MSRP Listener, there are also FourPacked declarations
    if (has_declarations) {
        size_t const num_decl_octets = fourpacked_octet_count(number_of_values);
        auto const decl_data = events_data.subspan(num_event_octets);

        if (decl_data.size() >= num_decl_octets) {
            out = std::format_to(out, "      Declarations: ");
            size_t decl_index = 0;
            for (size_t i = 0; i < num_decl_octets && decl_index < number_of_values; ++i) {
                auto const unpacked = unpack4_declarations(decl_data[i]);
                if (decl_index < number_of_values) {
                    out = std::format_to(
                        out, "{}", msrp::listener_declaration_name(static_cast<msrp::ListenerDeclaration>(unpacked.first)));
                    ++decl_index;
                }
                if (decl_index < number_of_values) {
                    out = std::format_to(
                        out, ", {}", msrp::listener_declaration_name(static_cast<msrp::ListenerDeclaration>(unpacked.second)));
                    ++decl_index;
                }
                if (decl_index < number_of_values) {
                    out = std::format_to(
                        out, ", {}", msrp::listener_declaration_name(static_cast<msrp::ListenerDeclaration>(unpacked.third)));
                    ++decl_index;
                }
                if (decl_index < number_of_values) {
                    out = std::format_to(
                        out, ", {}", msrp::listener_declaration_name(static_cast<msrp::ListenerDeclaration>(unpacked.fourth)));
                    ++decl_index;
                }
            }
            out = std::format_to(out, "\n");
        }
    }

    return out;
}

}  // namespace statusbar::srp::mrp
