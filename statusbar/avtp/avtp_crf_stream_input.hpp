#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// CRF stream input (listener/receiver) — clock reference recovery
///
/// Provides CrfStreamInputContext for tracking received CRF clock reference
/// timestamps and recovering the media clock. CRF timestamps are full 64-bit
/// gPTP-domain values (no 32→64 reconstruction needed unlike AM824/AAF).
///
/// References:
///   IEEE Std 1722-2016 Section 10 (Clock Reference Format)

#include "statusbar/avtp/avtp_crf.hpp"

#include <cstdint>
#include <span>

namespace statusbar::avtp {

/// Per-stream state for CRF deserialization (listener/receiver side).
/// One instance per received CRF stream.
///
/// CRF packets carry full 64-bit gPTP timestamps — no reconstruction needed.
/// The context tracks the most recent timestamps for media clock recovery,
/// detects sequence gaps, and monitors for media clock restarts.
struct CrfStreamInputContext
{
    // Config (from first valid packet or constructor)
    CrfType type{CrfType::audio_sample};
    uint32_t base_frequency{0};
    CrfPull pull{CrfPull::multiply_1_0};
    uint16_t timestamp_interval{0};

    // Running state
    uint8_t last_sequence_num{0};
    uint64_t last_timestamp_ns{0};   ///< Most recent received timestamp
    uint64_t first_timestamp_ns{0};  ///< First timestamp in most recent packet
    uint16_t last_timestamp_count{0};
    bool has_valid_data{false};
    bool media_clock_restarted{false};  ///< Set when mr bit detected

    // Statistics
    uint32_t packets_received{0};
    uint32_t sequence_gaps{0};
    uint64_t total_timestamps_received{0};

    /// Construct with expected CRF parameters
    CrfStreamInputContext(CrfType t, uint32_t base_freq, CrfPull p, uint16_t ts_interval);

    /// Default construct (config populated from first received packet)
    CrfStreamInputContext() = default;

    /// Reset running state while preserving configuration
    void reset() noexcept;

    /// Update sequence number and detect gaps
    void update_sequence_num(uint8_t seq_num) noexcept;

    /// Process a received CRF packet.
    /// Extracts timestamps from the payload, updates running state.
    /// @param pdu            The parsed CRF PDU header
    /// @param timestamp_data The timestamp payload (after the 20-byte header)
    /// @param on_timestamp   Callback: void(uint64_t timestamp_ns, uint16_t index)
    ///                       Called for each timestamp in the packet.
    template <typename TimestampCallback>
    void process_packet(CrfPdu const& pdu, std::span<uint8_t const> timestamp_data, TimestampCallback const& on_timestamp)
    {
        // Update config from packet if this is first packet
        if (packets_received == 0) {
            type = pdu.get_crf_type();
            base_frequency = pdu.base_frequency();
            pull = pdu.get_pull();
            timestamp_interval = pdu.timestamp_interval();
        }

        update_sequence_num(pdu.get_sequence_num());
        media_clock_restarted = pdu.mr();

        uint16_t const count = pdu.timestamp_count();
        size_t const available = timestamp_data.size() / CrfPdu::TIMESTAMP_SIZE;
        uint16_t const actual_count = static_cast<uint16_t>((count < available) ? count : available);

        for (uint16_t i = 0; i < actual_count; ++i) {
            auto const ts = crf_get_timestamp(timestamp_data, i);
            if (ts.has_value()) {
                uint64_t const ts_ns = *ts;
                if (i == 0) {
                    first_timestamp_ns = ts_ns;
                }
                last_timestamp_ns = ts_ns;
                on_timestamp(ts_ns, i);
            }
        }

        last_timestamp_count = actual_count;
        total_timestamps_received += actual_count;
        has_valid_data = (actual_count > 0);
        ++packets_received;
    }

    /// Get the nominal clock period in nanoseconds (1/frequency * 10^9)
    [[nodiscard]] auto nominal_period_ns() const noexcept -> double;
};

}  // namespace statusbar::avtp
