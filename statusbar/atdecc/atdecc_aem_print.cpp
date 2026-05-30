// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/atdecc/atdecc_aem_format.hpp"

namespace statusbar::atdecc::aem {

auto parse_descriptor(std::span<uint8_t const> desc_data) -> StatusValue<ParsedDescriptor const*>
{
    // Need at least 4 bytes for descriptor_type and descriptor_index
    if (desc_data.size() < 4) {
        return failure(BufferError::insufficient_data);
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto const* parsed_descriptor = reinterpret_cast<ParsedDescriptor const*>(desc_data.data());
    uint16_t const desc_type = static_cast<uint16_t>(parsed_descriptor->data.common.descriptor_type);

    // Look up minimum length from table
    size_t min_length = 4;  // Default: just need type and index
    if (desc_type < NUM_DESCRIPTOR_TYPES && descriptor_min_lengths[desc_type] > 0) {
        min_length = descriptor_min_lengths[desc_type];
    }

    if (desc_data.size() < min_length) {
        return failure(BufferError::insufficient_data);
    }

    return success(parsed_descriptor);
}

}  // namespace statusbar::atdecc::aem

namespace statusbar::atdecc {

auto parse_aem(uint16_t const cmd, bool const is_response, std::span<uint8_t const> payload)
    -> StatusValue<aem::ParsedAemPayload const*>
{
    // Look up minimum length from table
    size_t min_length = 0;
    if (cmd < aem::NUM_AEM_COMMANDS) {
        auto const& lengths = aem_command_min_lengths[cmd];
        min_length = is_response && lengths.response > 0 ? lengths.response : lengths.command;
    }

    if (min_length > 0 && payload.size() < min_length) {
        return failure(BufferError::insufficient_data);
    }

    // Return pointer to payload data as ParsedAemPayload
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return success(reinterpret_cast<aem::ParsedAemPayload const*>(payload.data()));
}

}  // namespace statusbar::atdecc
