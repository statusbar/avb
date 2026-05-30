#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Build and parse SET_CONTROL / GET_CONTROL AEM command payloads
/// against a DescriptorControl. Also provides per-family validation
/// helpers for the common case where a controller wants to check a
/// proposed new `current` value against the descriptor's min / max /
/// step / options / default before putting it on the wire.
///
/// Workflow:
///
///   DescriptorControl d = /* read from entity via READ_DESCRIPTOR */;
///   // …inspect d.control_type, d.control_value_type, d.number_of_values,
///   // and use linear_entry<T>/selector_view<T>/array_view<T> from
///   // atdecc_aem_control_accessors.hpp to present values to the user.
///
///   // Validate the new value:
///   if (!validate_linear_current<uint8_t>(d, 0, new_value)) { /* reject */ }
///
///   // Update the descriptor's current field(s), then serialize:
///   auto entry = *linear_entry<uint8_t>(d, 0);
///   entry.set_current(new_value);
///   std::array<LinearValueEntry<uint8_t>, 1> arr{entry};
///   init_linear<uint8_t>(d, std::span{arr});
///   std::array<uint8_t, 512> buf;
///   auto n = build_set_control_payload(d, buf);
///   // send buf.first(*n) inside an AemDu with AEM_COMMAND_SET_CONTROL

#include "statusbar/atdecc/atdecc_aem_command.hpp"
#include "statusbar/atdecc/atdecc_aem_control_accessors.hpp"
#include "statusbar/atdecc/atdecc_aem_descriptor.hpp"
#include "statusbar/status/status.hpp"

#include <algorithm>
#include <cstring>
#include <expected>
#include <span>
#include <system_error>

namespace statusbar::atdecc::aem {

/// Serialize a DescriptorControl's current value_details as a
/// SET_CONTROL (or GET_CONTROL response) payload:
///
///   descriptor_type  (2 bytes; always DESCRIPTOR_CONTROL)
///   descriptor_index (2 bytes)
///   value_details    (L bytes; L = d.value_details_length())
///
/// Returns the total byte count written (AemControlPayloadHeader::LENGTH
/// + L), or std::errc::message_size if `out` is too small.
///
/// Callers should update the descriptor's value_details (typically via
/// linear_entry<T> + init_linear<T>, or the matching selector/array/
/// special init_* helpers) before calling this — the payload is a
/// straight serialization of the descriptor's existing state.
[[nodiscard]] inline auto build_set_control_payload(DescriptorControl const& d, std::span<uint8_t> out) noexcept
    -> std::expected<size_t, std::error_code>
{
    size_t const body_len = d.value_details_length();
    size_t const total = AemControlPayloadHeader::LENGTH + body_len;
    if (out.size() < total) {
        return std::unexpected(std::make_error_code(std::errc::message_size));
    }
    AemControlPayloadHeader header{};
    header.descriptor_type = static_cast<uint16_t>(DESCRIPTOR_CONTROL);
    header.descriptor_index = d.descriptor_index.get();
    std::memcpy(out.data(), &header, AemControlPayloadHeader::LENGTH);
    if (body_len > 0) {
        std::memcpy(out.data() + AemControlPayloadHeader::LENGTH, d.value_details.data(), body_len);
    }
    return total;
}

/// Parse a SET_CONTROL / GET_CONTROL response payload and copy the
/// value_details back into the descriptor. The caller's `d` provides
/// the expected control_value_type; the response's length must be
/// consistent with d.value_details_length(). The header's
/// descriptor_index is verified against d.descriptor_index.
///
/// Returns:
/// - std::errc::message_size      if payload is shorter than the header
///                                or the body length disagrees with the
///                                declared value_type/number_of_values
/// - std::errc::wrong_protocol_type  if descriptor_type != CONTROL
/// - std::errc::no_such_device    if descriptor_index mismatches
[[nodiscard]] inline auto parse_control_response(DescriptorControl& d, std::span<uint8_t const> payload) noexcept
    -> std::expected<void, std::error_code>
{
    if (payload.size() < AemControlPayloadHeader::LENGTH) {
        return std::unexpected(std::make_error_code(std::errc::message_size));
    }
    AemControlPayloadHeader header{};
    std::memcpy(&header, payload.data(), AemControlPayloadHeader::LENGTH);
    if (header.descriptor_type.get() != DESCRIPTOR_CONTROL) {
        return std::unexpected(std::make_error_code(std::errc::wrong_protocol_type));
    }
    if (header.descriptor_index.get() != d.descriptor_index.get()) {
        return std::unexpected(std::make_error_code(std::errc::no_such_device));
    }
    size_t const expected_body = d.value_details_length();
    size_t const actual_body = payload.size() - AemControlPayloadHeader::LENGTH;
    if (expected_body == 0) {
        // Variable-length family (UTF-8, VENDOR) — accept whatever came on the wire.
        size_t const n = std::min(actual_body, DescriptorControl::MAX_VALUE_DETAILS);
        std::memcpy(d.value_details.data(), payload.data() + AemControlPayloadHeader::LENGTH, n);
        for (size_t i = n; i < DescriptorControl::MAX_VALUE_DETAILS; ++i) {
            d.value_details[i] = 0;
        }
        return {};
    }
    if (actual_body < expected_body) {
        return std::unexpected(std::make_error_code(std::errc::message_size));
    }
    std::memcpy(d.value_details.data(), payload.data() + AemControlPayloadHeader::LENGTH, expected_body);
    for (size_t i = expected_body; i < DescriptorControl::MAX_VALUE_DETAILS; ++i) {
        d.value_details[i] = 0;
    }
    return {};
}

//
// Validation helpers
//

/// Check if `value` is a legal current value for entry `entry_index` of
/// a linear-family CONTROL descriptor. Returns true iff:
///   - d.control_value_type matches CONTROL_LINEAR_<T>
///   - entry_index < d.number_of_values
///   - minimum <= value <= maximum
///   - (value - minimum) is a multiple of step (integer types only;
///     for floating-point T step congruence is not enforced — callers
///     that need floating-point step snap should do it explicitly)
template <typename T>
[[nodiscard]] auto validate_linear_current(DescriptorControl const& d, size_t entry_index, T value) noexcept -> bool
{
    auto entry = linear_entry<T>(d, entry_index);
    if (!entry) {
        return false;
    }
    T const lo = entry->minimum();
    T const hi = entry->maximum();
    T const step = entry->step();
    if (value < lo || value > hi) {
        return false;
    }
    if constexpr (std::is_integral_v<T>) {
        if (step != 0) {
            // Use the unsigned difference to avoid overflow for
            // full-range signed types.
            using UT = std::make_unsigned_t<T>;
            UT const delta = static_cast<UT>(value) - static_cast<UT>(lo);
            UT const ustep = static_cast<UT>(step);
            if ((delta % ustep) != 0) {
                return false;
            }
        }
    }
    return true;
}

/// Check if `value` is one of the legal options of a selector-family
/// CONTROL descriptor. Returns true iff:
///   - d.control_value_type matches CONTROL_SELECTOR_<T>
///   - value appears in the options list (or equals current / default —
///     those are always accepted even if not in options, matching
///     how some entities allow "current" to be a transient value)
template <typename T>
[[nodiscard]] auto validate_selector_current(DescriptorControl const& d, T value) -> bool
{
    auto view = selector_view<T>(d);
    if (!view) {
        return false;
    }
    if (value == view->current || value == view->default_value) {
        return true;
    }
    for (auto const& opt : view->options) {
        if (value == opt) {
            return true;
        }
    }
    return false;
}

/// Check if `value` is a legal current value for an array-family
/// CONTROL descriptor. Array controls share one min/max/step/default
/// across all N entries.
template <typename T>
[[nodiscard]] auto validate_array_current(DescriptorControl const& d, T value) -> bool
{
    auto view = array_view<T>(d);
    if (!view) {
        return false;
    }
    T const lo = view->minimum;
    T const hi = view->maximum;
    T const step = view->step;
    if (value < lo || value > hi) {
        return false;
    }
    if constexpr (std::is_integral_v<T>) {
        if (step != 0) {
            using UT = std::make_unsigned_t<T>;
            UT const delta = static_cast<UT>(value) - static_cast<UT>(lo);
            UT const ustep = static_cast<UT>(step);
            if ((delta % ustep) != 0) {
                return false;
            }
        }
    }
    return true;
}

//
// Convenience: update a single current value and build the SET_CONTROL
// payload in one call (validates unless validate=false).
//

/// Replace the `current` field of the `entry_index`-th linear entry with
/// `new_value` and serialize a SET_CONTROL payload. Leaves the
/// descriptor with the updated value_details (so subsequent reads see
/// the new state). Returns std::errc::invalid_argument if validation
/// fails.
template <typename T>
[[nodiscard]] auto build_set_control_linear_current(
    DescriptorControl& d, size_t entry_index, T new_value, std::span<uint8_t> out, bool validate = true) noexcept
    -> std::expected<size_t, std::error_code>
{
    auto entry = linear_entry<T>(d, entry_index);
    if (!entry) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    if (validate && !validate_linear_current<T>(d, entry_index, new_value)) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    entry->set_current(new_value);
    size_t const entry_size = sizeof(LinearValueEntry<T>);
    size_t const offset = entry_index * entry_size;
    std::memcpy(d.value_details.data() + offset, &*entry, entry_size);
    return build_set_control_payload(d, out);
}

/// Replace the selector's `current` field with `new_value` and serialize
/// the SET_CONTROL payload.
template <typename T>
[[nodiscard]] auto build_set_control_selector_current(
    DescriptorControl& d, T new_value, std::span<uint8_t> out, bool validate = true) -> std::expected<size_t, std::error_code>
{
    auto view = selector_view<T>(d);
    if (!view) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    if (validate && !validate_selector_current<T>(d, new_value)) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    view->current = new_value;
    if (!init_selector<T>(d, *view)) {
        return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
    }
    return build_set_control_payload(d, out);
}

}  // namespace statusbar::atdecc::aem
