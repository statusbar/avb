#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Typed readers and writers for the variable-length value_details
/// trailer of an AEM CONTROL descriptor. All accessors are free
/// functions taking a `DescriptorControl` (or const reference) — they
/// dispatch on the descriptor's `control_value_type` and return typed
/// values only when the requested family / element type matches.
///
/// Reads copy out by value (memcpy from the byte trailer) so that the
/// raw buffer alignment is never assumed. Writes encode into the
/// buffer and update the descriptor's number_of_values and
/// control_value_type bookkeeping.
///
/// Each family has:
/// - a reader returning `std::optional<T>` (returns nullopt if the
///   descriptor's value_type does not match the requested T)
/// - an initializer that populates the descriptor from typed values,
///   returning `false` if the payload would exceed MAX_VALUE_DETAILS
///
/// Validation (min/max/step checks) lives with the SET_CONTROL payload
/// builder — see atdecc_aem_control_command.hpp (commit 5).

#include "statusbar/atdecc/atdecc_aem_control_values.hpp"
#include "statusbar/atdecc/atdecc_aem_descriptor.hpp"

#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace statusbar::atdecc::aem {

//
// Linear family
//

namespace detail {

/// Map a C++ element type to the expected CONTROL_LINEAR_* value_type.
template <typename T>
[[nodiscard]] constexpr auto linear_value_type_for() noexcept -> uint16_t
{
    if constexpr (std::is_same_v<T, int8_t>) {
        return CONTROL_LINEAR_INT8;
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        return CONTROL_LINEAR_UINT8;
    } else if constexpr (std::is_same_v<T, int16_t>) {
        return CONTROL_LINEAR_INT16;
    } else if constexpr (std::is_same_v<T, uint16_t>) {
        return CONTROL_LINEAR_UINT16;
    } else if constexpr (std::is_same_v<T, int32_t>) {
        return CONTROL_LINEAR_INT32;
    } else if constexpr (std::is_same_v<T, uint32_t>) {
        return CONTROL_LINEAR_UINT32;
    } else if constexpr (std::is_same_v<T, int64_t>) {
        return CONTROL_LINEAR_INT64;
    } else if constexpr (std::is_same_v<T, uint64_t>) {
        return CONTROL_LINEAR_UINT64;
    } else if constexpr (std::is_same_v<T, float>) {
        return CONTROL_LINEAR_FLOAT;
    } else if constexpr (std::is_same_v<T, double>) {
        return CONTROL_LINEAR_DOUBLE;
    } else {
        static_assert(sizeof(T) == 0, "Unsupported linear value element type");
        return 0;
    }
}

template <typename T>
[[nodiscard]] constexpr auto selector_value_type_for() noexcept -> uint16_t
{
    if constexpr (std::is_same_v<T, int8_t>) {
        return CONTROL_SELECTOR_INT8;
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        return CONTROL_SELECTOR_UINT8;
    } else if constexpr (std::is_same_v<T, int16_t>) {
        return CONTROL_SELECTOR_INT16;
    } else if constexpr (std::is_same_v<T, uint16_t>) {
        return CONTROL_SELECTOR_UINT16;
    } else if constexpr (std::is_same_v<T, int32_t>) {
        return CONTROL_SELECTOR_INT32;
    } else if constexpr (std::is_same_v<T, uint32_t>) {
        return CONTROL_SELECTOR_UINT32;
    } else if constexpr (std::is_same_v<T, int64_t>) {
        return CONTROL_SELECTOR_INT64;
    } else if constexpr (std::is_same_v<T, uint64_t>) {
        return CONTROL_SELECTOR_UINT64;
    } else if constexpr (std::is_same_v<T, float>) {
        return CONTROL_SELECTOR_FLOAT;
    } else if constexpr (std::is_same_v<T, double>) {
        return CONTROL_SELECTOR_DOUBLE;
    } else {
        static_assert(sizeof(T) == 0, "Unsupported selector value element type (use uint16_t for STRING references)");
        return 0;
    }
}

template <typename T>
[[nodiscard]] constexpr auto array_value_type_for() noexcept -> uint16_t
{
    if constexpr (std::is_same_v<T, int8_t>) {
        return CONTROL_ARRAY_INT8;
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        return CONTROL_ARRAY_UINT8;
    } else if constexpr (std::is_same_v<T, int16_t>) {
        return CONTROL_ARRAY_INT16;
    } else if constexpr (std::is_same_v<T, uint16_t>) {
        return CONTROL_ARRAY_UINT16;
    } else if constexpr (std::is_same_v<T, int32_t>) {
        return CONTROL_ARRAY_INT32;
    } else if constexpr (std::is_same_v<T, uint32_t>) {
        return CONTROL_ARRAY_UINT32;
    } else if constexpr (std::is_same_v<T, int64_t>) {
        return CONTROL_ARRAY_INT64;
    } else if constexpr (std::is_same_v<T, uint64_t>) {
        return CONTROL_ARRAY_UINT64;
    } else if constexpr (std::is_same_v<T, float>) {
        return CONTROL_ARRAY_FLOAT;
    } else if constexpr (std::is_same_v<T, double>) {
        return CONTROL_ARRAY_DOUBLE;
    } else {
        static_assert(sizeof(T) == 0, "Unsupported array value element type");
        return 0;
    }
}

}  // namespace detail

/// Read a single linear-family value entry by index. Returns nullopt if
/// the descriptor's value_type does not match T or if `index >=
/// number_of_values`. `index == 0` for the common single-value case.
template <typename T>
[[nodiscard]] auto linear_entry(DescriptorControl const& d, size_t index) noexcept -> std::optional<LinearValueEntry<T>>
{
    auto const bits = unpack_control_value_type(d.control_value_type.get());
    if (bits.value_type != detail::linear_value_type_for<T>()) {
        return std::nullopt;
    }
    if (index >= d.number_of_values.get()) {
        return std::nullopt;
    }
    size_t const entry_size = sizeof(LinearValueEntry<T>);
    size_t const offset = index * entry_size;
    if (offset + entry_size > DescriptorControl::MAX_VALUE_DETAILS) {
        return std::nullopt;
    }
    LinearValueEntry<T> entry{};
    std::memcpy(&entry, d.value_details.data() + offset, entry_size);
    return entry;
}

/// Populate the descriptor as a CONTROL_LINEAR_* control. Copies each
/// LinearValueEntry<T> into the value_details trailer, sets
/// control_value_type (including optional read-only/unknown flags),
/// number_of_values, and values_offset. Returns false if the payload
/// would exceed MAX_VALUE_DETAILS (callers may trim `entries` first).
template <typename T>
[[nodiscard]] auto init_linear(
    DescriptorControl& d, std::span<LinearValueEntry<T> const> entries, bool read_only = false, bool unknown = false) noexcept
    -> bool
{
    size_t const n = entries.size();
    size_t const entry_size = sizeof(LinearValueEntry<T>);
    size_t const total = n * entry_size;
    if (total > DescriptorControl::MAX_VALUE_DETAILS) {
        return false;
    }
    d.control_value_type = pack_control_value_type(read_only, unknown, detail::linear_value_type_for<T>());
    d.values_offset = static_cast<uint16_t>(DescriptorControl::LENGTH);
    d.number_of_values = static_cast<uint16_t>(n);
    for (size_t i = 0; i < n; ++i) {
        std::memcpy(d.value_details.data() + (i * entry_size), &entries[i], entry_size);
    }
    // Zero any tail bytes beyond the new payload.
    for (size_t i = total; i < DescriptorControl::MAX_VALUE_DETAILS; ++i) {
        d.value_details[i] = 0;
    }
    return true;
}

//
// Selector family — view with current/default/options/unit
//
// Wire layout: current(V) + default(V) + option[0..N-1](V each) + unit(2)
//

template <typename T>
struct SelectorView
{
    T current{};
    T default_value{};
    std::vector<T> options{};
    ControlUnits unit{};
};

/// Read the selector value_details as typed values. Returns nullopt if
/// the descriptor's value_type does not match T. options is a heap
/// allocation sized to `number_of_values`.
template <typename T>
[[nodiscard]] auto selector_view(DescriptorControl const& d) -> std::optional<SelectorView<T>>
{
    auto const bits = unpack_control_value_type(d.control_value_type.get());
    if (bits.value_type != detail::selector_value_type_for<T>()) {
        return std::nullopt;
    }
    size_t const v = sizeof(T);
    size_t const n = d.number_of_values.get();
    size_t const total = ((n + 2) * v) + 2;
    if (total > DescriptorControl::MAX_VALUE_DETAILS) {
        return std::nullopt;
    }
    SelectorView<T> view{};
    auto const* base = d.value_details.data();
    std::array<uint8_t, sizeof(T)> buf{};
    std::memcpy(buf.data(), base, v);
    view.current = detail::decode_linear_field<T>(buf);
    std::memcpy(buf.data(), base + v, v);
    view.default_value = detail::decode_linear_field<T>(buf);
    view.options.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        std::memcpy(buf.data(), base + ((i + 2) * v), v);
        view.options.push_back(detail::decode_linear_field<T>(buf));
    }
    std::memcpy(&view.unit, base + ((n + 2) * v), sizeof(ControlUnits));
    return view;
}

/// Populate the descriptor as a CONTROL_SELECTOR_* control. Encodes
/// current, default, all options (|options| = number_of_values), and
/// the unit into the value_details trailer.
template <typename T>
[[nodiscard]] auto init_selector(
    DescriptorControl& d, SelectorView<T> const& view, bool read_only = false, bool unknown = false) noexcept -> bool
{
    size_t const v = sizeof(T);
    size_t const n = view.options.size();
    size_t const total = ((n + 2) * v) + 2;
    if (total > DescriptorControl::MAX_VALUE_DETAILS) {
        return false;
    }
    d.control_value_type = pack_control_value_type(read_only, unknown, detail::selector_value_type_for<T>());
    d.values_offset = static_cast<uint16_t>(DescriptorControl::LENGTH);
    d.number_of_values = static_cast<uint16_t>(n);
    auto* base = d.value_details.data();
    std::array<uint8_t, sizeof(T)> buf{};
    detail::encode_linear_field<T>(view.current, buf);
    std::memcpy(base, buf.data(), v);
    detail::encode_linear_field<T>(view.default_value, buf);
    std::memcpy(base + v, buf.data(), v);
    for (size_t i = 0; i < n; ++i) {
        detail::encode_linear_field<T>(view.options[i], buf);
        std::memcpy(base + ((i + 2) * v), buf.data(), v);
    }
    std::memcpy(base + ((n + 2) * v), &view.unit, sizeof(ControlUnits));
    for (size_t i = total; i < DescriptorControl::MAX_VALUE_DETAILS; ++i) {
        d.value_details[i] = 0;
    }
    return true;
}

//
// Array family — view with min/max/step/default/unit/string + current[0..N-1]
//
// Wire layout: min(V) + max(V) + step(V) + default(V) + unit(2) + string(2)
//              + current[0..N-1](V each)
//

template <typename T>
struct ArrayView
{
    T minimum{};
    T maximum{};
    T step{};
    T default_value{};
    ControlUnits unit{};
    doublet_t localized_string{0};
    std::vector<T> current{};
};

template <typename T>
[[nodiscard]] auto array_view(DescriptorControl const& d) -> std::optional<ArrayView<T>>
{
    auto const bits = unpack_control_value_type(d.control_value_type.get());
    if (bits.value_type != detail::array_value_type_for<T>()) {
        return std::nullopt;
    }
    size_t const v = sizeof(T);
    size_t const n = d.number_of_values.get();
    size_t const total = 4 + (4 * v) + (n * v);
    if (total > DescriptorControl::MAX_VALUE_DETAILS) {
        return std::nullopt;
    }
    ArrayView<T> view{};
    auto const* base = d.value_details.data();
    std::array<uint8_t, sizeof(T)> buf{};
    auto const decode_at = [&](size_t offset) -> T {
        std::memcpy(buf.data(), base + offset, v);
        return detail::decode_linear_field<T>(buf);
    };
    view.minimum = decode_at(0);
    view.maximum = decode_at(v);
    view.step = decode_at(2 * v);
    view.default_value = decode_at(3 * v);
    std::memcpy(&view.unit, base + (4 * v), sizeof(ControlUnits));
    std::memcpy(&view.localized_string, base + (4 * v) + 2, sizeof(doublet_t));
    view.current.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        view.current.push_back(decode_at(4 + (4 * v) + (i * v)));
    }
    return view;
}

template <typename T>
[[nodiscard]] auto init_array(DescriptorControl& d, ArrayView<T> const& view, bool read_only = false, bool unknown = false) noexcept
    -> bool
{
    size_t const v = sizeof(T);
    size_t const n = view.current.size();
    size_t const total = 4 + (4 * v) + (n * v);
    if (total > DescriptorControl::MAX_VALUE_DETAILS) {
        return false;
    }
    d.control_value_type = pack_control_value_type(read_only, unknown, detail::array_value_type_for<T>());
    d.values_offset = static_cast<uint16_t>(DescriptorControl::LENGTH);
    d.number_of_values = static_cast<uint16_t>(n);
    auto* base = d.value_details.data();
    std::array<uint8_t, sizeof(T)> buf{};
    auto const encode_at = [&](T val, size_t offset) -> void {
        detail::encode_linear_field<T>(val, buf);
        std::memcpy(base + offset, buf.data(), v);
    };
    encode_at(view.minimum, 0);
    encode_at(view.maximum, v);
    encode_at(view.step, 2 * v);
    encode_at(view.default_value, 3 * v);
    std::memcpy(base + (4 * v), &view.unit, sizeof(ControlUnits));
    std::memcpy(base + (4 * v) + 2, &view.localized_string, sizeof(doublet_t));
    for (size_t i = 0; i < n; ++i) {
        encode_at(view.current[i], 4 + (4 * v) + (i * v));
    }
    for (size_t i = total; i < DescriptorControl::MAX_VALUE_DETAILS; ++i) {
        d.value_details[i] = 0;
    }
    return true;
}

//
// UTF-8 string value
//

/// Read the CONTROL_UTF8 string (NUL-terminated). Returns nullopt if
/// the descriptor is not CONTROL_UTF8. The length S is inferred from
/// the NUL terminator within the first MAX_VALUE_DETAILS bytes.
[[nodiscard]] inline auto control_utf8(DescriptorControl const& d) noexcept -> std::optional<std::string_view>
{
    auto const bits = unpack_control_value_type(d.control_value_type.get());
    if (bits.value_type != CONTROL_UTF8) {
        return std::nullopt;
    }
    char const* const base = reinterpret_cast<char const*>(d.value_details.data());
    size_t n = 0;
    while (n < DescriptorControl::MAX_VALUE_DETAILS && base[n] != '\0') {
        ++n;
    }
    return std::string_view{base, n};
}

/// Populate the descriptor as a CONTROL_UTF8 control. Writes the bytes
/// of `text` followed by a NUL terminator. Returns false if
/// text.size() + 1 > MAX_VALUE_DETAILS.
[[nodiscard]] inline auto init_utf8(
    DescriptorControl& d, std::string_view text, bool read_only = false, bool unknown = false) noexcept -> bool
{
    if (text.size() + 1 > DescriptorControl::MAX_VALUE_DETAILS) {
        return false;
    }
    d.control_value_type = pack_control_value_type(read_only, unknown, CONTROL_UTF8);
    d.values_offset = static_cast<uint16_t>(DescriptorControl::LENGTH);
    // CONTROL_UTF8's "N" field is 1 per Table 7.11; we still set it so
    // round-tripping through a descriptor table is consistent.
    d.number_of_values = 1;
    std::memcpy(d.value_details.data(), text.data(), text.size());
    d.value_details[text.size()] = 0;
    for (size_t i = text.size() + 1; i < DescriptorControl::MAX_VALUE_DETAILS; ++i) {
        d.value_details[i] = 0;
    }
    return true;
}

//
// SMPTE time / Sample rate / gPTP time — fixed-size specials
//

[[nodiscard]] inline auto control_smpte_time(DescriptorControl const& d) noexcept -> std::optional<SmpteTimeValue>
{
    auto const bits = unpack_control_value_type(d.control_value_type.get());
    if (bits.value_type != CONTROL_SMPTE_TIME) {
        return std::nullopt;
    }
    SmpteTimeValue v{};
    std::memcpy(&v, d.value_details.data(), sizeof(SmpteTimeValue));
    return v;
}

inline auto init_smpte_time(DescriptorControl& d, SmpteTimeValue const& v, bool read_only = false, bool unknown = false) noexcept
    -> void
{
    d.control_value_type = pack_control_value_type(read_only, unknown, CONTROL_SMPTE_TIME);
    d.values_offset = static_cast<uint16_t>(DescriptorControl::LENGTH);
    d.number_of_values = 1;
    std::memcpy(d.value_details.data(), &v, sizeof(SmpteTimeValue));
    for (size_t i = sizeof(SmpteTimeValue); i < DescriptorControl::MAX_VALUE_DETAILS; ++i) {
        d.value_details[i] = 0;
    }
}

[[nodiscard]] inline auto control_sample_rate(DescriptorControl const& d) noexcept -> std::optional<SampleRateValue>
{
    auto const bits = unpack_control_value_type(d.control_value_type.get());
    if (bits.value_type != CONTROL_SAMPLE_RATE) {
        return std::nullopt;
    }
    SampleRateValue v{};
    std::memcpy(&v, d.value_details.data(), sizeof(SampleRateValue));
    return v;
}

inline auto init_sample_rate(DescriptorControl& d, SampleRateValue const& v, bool read_only = false, bool unknown = false) noexcept
    -> void
{
    d.control_value_type = pack_control_value_type(read_only, unknown, CONTROL_SAMPLE_RATE);
    d.values_offset = static_cast<uint16_t>(DescriptorControl::LENGTH);
    d.number_of_values = 1;
    std::memcpy(d.value_details.data(), &v, sizeof(SampleRateValue));
    for (size_t i = sizeof(SampleRateValue); i < DescriptorControl::MAX_VALUE_DETAILS; ++i) {
        d.value_details[i] = 0;
    }
}

[[nodiscard]] inline auto control_gptp_time(DescriptorControl const& d) noexcept -> std::optional<GptpTimeValue>
{
    auto const bits = unpack_control_value_type(d.control_value_type.get());
    if (bits.value_type != CONTROL_GPTP_TIME) {
        return std::nullopt;
    }
    GptpTimeValue v{};
    std::memcpy(&v, d.value_details.data(), sizeof(GptpTimeValue));
    return v;
}

inline auto init_gptp_time(DescriptorControl& d, GptpTimeValue const& v, bool read_only = false, bool unknown = false) noexcept
    -> void
{
    d.control_value_type = pack_control_value_type(read_only, unknown, CONTROL_GPTP_TIME);
    d.values_offset = static_cast<uint16_t>(DescriptorControl::LENGTH);
    d.number_of_values = 1;
    std::memcpy(d.value_details.data(), &v, sizeof(GptpTimeValue));
    for (size_t i = sizeof(GptpTimeValue); i < DescriptorControl::MAX_VALUE_DETAILS; ++i) {
        d.value_details[i] = 0;
    }
}

//
// Vendor blob
//

[[nodiscard]] inline auto control_vendor(DescriptorControl const& d) noexcept -> std::optional<std::span<uint8_t const>>
{
    auto const bits = unpack_control_value_type(d.control_value_type.get());
    if (bits.value_type != CONTROL_VENDOR) {
        return std::nullopt;
    }
    // CONTROL_VENDOR's length U is not derivable from number_of_values;
    // callers typically use the AECP control_data_length from the wire
    // to size the span. We return the full buffer and let the caller
    // trim; if the caller has already memcpy'd exactly U bytes they can
    // treat the first U bytes as the payload.
    return std::span<uint8_t const>{d.value_details};
}

inline auto init_vendor(DescriptorControl& d, std::span<uint8_t const> blob, bool read_only = false, bool unknown = false) noexcept
    -> bool
{
    if (blob.size() > DescriptorControl::MAX_VALUE_DETAILS) {
        return false;
    }
    d.control_value_type = pack_control_value_type(read_only, unknown, CONTROL_VENDOR);
    d.values_offset = static_cast<uint16_t>(DescriptorControl::LENGTH);
    d.number_of_values = 1;
    std::memcpy(d.value_details.data(), blob.data(), blob.size());
    for (size_t i = blob.size(); i < DescriptorControl::MAX_VALUE_DETAILS; ++i) {
        d.value_details[i] = 0;
    }
    return true;
}

//
// Bode plot
//

/// Read the fixed 48-byte header of a CONTROL_BODE_PLOT descriptor.
[[nodiscard]] inline auto control_bode_plot_header(DescriptorControl const& d) noexcept -> std::optional<BodePlotHeader>
{
    auto const bits = unpack_control_value_type(d.control_value_type.get());
    if (bits.value_type != CONTROL_BODE_PLOT) {
        return std::nullopt;
    }
    BodePlotHeader h{};
    std::memcpy(&h, d.value_details.data(), sizeof(BodePlotHeader));
    return h;
}

/// Read a single current {frequency, magnitude, phase} triple by index.
[[nodiscard]] inline auto control_bode_plot_point(DescriptorControl const& d, size_t index) noexcept -> std::optional<BodePlotPoint>
{
    auto const bits = unpack_control_value_type(d.control_value_type.get());
    if (bits.value_type != CONTROL_BODE_PLOT) {
        return std::nullopt;
    }
    if (index >= d.number_of_values.get()) {
        return std::nullopt;
    }
    size_t const offset = sizeof(BodePlotHeader) + (index * sizeof(BodePlotPoint));
    if (offset + sizeof(BodePlotPoint) > DescriptorControl::MAX_VALUE_DETAILS) {
        return std::nullopt;
    }
    BodePlotPoint p{};
    std::memcpy(&p, d.value_details.data() + offset, sizeof(BodePlotPoint));
    return p;
}

/// Populate the descriptor as a CONTROL_BODE_PLOT control.
inline auto init_bode_plot(
    DescriptorControl& d,
    BodePlotHeader const& header,
    std::span<BodePlotPoint const> points,
    bool read_only = false,
    bool unknown = false) noexcept -> bool
{
    size_t const total = sizeof(BodePlotHeader) + (points.size() * sizeof(BodePlotPoint));
    if (total > DescriptorControl::MAX_VALUE_DETAILS) {
        return false;
    }
    d.control_value_type = pack_control_value_type(read_only, unknown, CONTROL_BODE_PLOT);
    d.values_offset = static_cast<uint16_t>(DescriptorControl::LENGTH);
    d.number_of_values = static_cast<uint16_t>(points.size());
    std::memcpy(d.value_details.data(), &header, sizeof(BodePlotHeader));
    for (size_t i = 0; i < points.size(); ++i) {
        std::memcpy(
            d.value_details.data() + sizeof(BodePlotHeader) + (i * sizeof(BodePlotPoint)), &points[i], sizeof(BodePlotPoint));
    }
    for (size_t i = total; i < DescriptorControl::MAX_VALUE_DETAILS; ++i) {
        d.value_details[i] = 0;
    }
    return true;
}

}  // namespace statusbar::atdecc::aem
