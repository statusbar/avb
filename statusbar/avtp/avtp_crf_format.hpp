#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// format_to() overloads for AVTP CrfPdu and CRF timestamp data. Split
/// from avtp_crf.hpp so consumers that only need the data structures
/// do not pay the compile-time cost of <format>.

#include "statusbar/avtp/avtp_crf.hpp"
#include "statusbar/tsn/tsn_stream_id_format.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>

namespace statusbar::avtp {

/// Format a CrfPdu to an output iterator
/// @param out The output iterator to write formatted text to
/// @param crf The CRF PDU to format
template <typename OutputIt>
auto format_to(OutputIt out, CrfPdu const& crf) -> OutputIt
{
    out = std::format_to(out, "CRF: {} stream_id=", crf_type_name(crf.get_type()));
    out = tsn::format_to(out, crf.stream_id());

    out = std::format_to(
        out,
        "\n        seq={} sv={} mr={} fs={} tu={}",
        crf.get_sequence_num(),
        crf.sv() ? 1 : 0,
        crf.mr() ? 1 : 0,
        crf.fs() ? 1 : 0,
        crf.tu() ? 1 : 0);

    out = std::format_to(
        out,
        "\n        base_freq={} Hz pull={} nominal_freq={:.3f} Hz",
        crf.base_frequency(),
        crf_pull_name(crf.pull()),
        crf.nominal_frequency());

    out = std::format_to(
        out,
        "\n        crf_data_length={} ({} timestamps) interval={}",
        crf.crf_data_length(),
        crf.timestamp_count(),
        crf.timestamp_interval());

    return out;
}

/// Format CRF timestamps to an output iterator (shows first few timestamps)
/// @param out The output iterator to write formatted text to
/// @param timestamp_data Raw timestamp data bytes from the CRF packet
/// @param max_display Maximum number of timestamps to display
template <typename OutputIt>
auto format_timestamps_to(OutputIt out, std::span<uint8_t const> const timestamp_data, size_t const max_display = 4) -> OutputIt
{
    size_t const count = timestamp_data.size() / CrfPdu::TIMESTAMP_SIZE;
    size_t const display_count = (count < max_display) ? count : max_display;

    for (size_t i = 0; i < display_count; ++i) {
        auto const ts = crf_get_timestamp(timestamp_data, i);
        if (ts.has_value()) {
            out = std::format_to(out, "\n        timestamp[{}]={} ns", i, *ts);
        }
    }

    if (count > max_display) {
        out = std::format_to(out, "\n        ... ({} more timestamps)", count - max_display);
    }

    return out;
}

}  // namespace statusbar::avtp
