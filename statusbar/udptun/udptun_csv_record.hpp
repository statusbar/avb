#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/colbin/colbin.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/itc/itc_message_pipe.hpp"
#include "statusbar/itc/itc_telemetry_counter.hpp"
#include "statusbar/udptun/udptun_codec_concept.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace statusbar::udptun {

/// One per received packet — codec-agnostic, trivially copyable so it
/// can travel through itc::QueuedPipe (the SPSC ring between the
/// RX hot path and the writer thread). 48 bytes on aarch64-linux.
struct UdpTunCsvRecord
{
    int64_t rx_gptp_ns;
    int64_t presentation_time_ns;
    int64_t latency_ns;
    ieee::Eui64 sender_id;
    uint32_t sequence;
    uint32_t interval_us;
    uint8_t role;     // PacketRole enum value
    uint8_t _pad[7];  // 7  → 48 bytes total
};

static_assert(sizeof(UdpTunCsvRecord) == 48, "UdpTunCsvRecord should be 48 bytes");

/// SPSC ring capacity for the CSV writer thread. 65536 records × 48 B
/// ≈ 3 MB; at 2000 rec/s (1 kHz tx-interval × primary+redundant) this
/// covers ~32 seconds of writer-thread I/O stall before drops start.
inline constexpr size_t csv_ring_capacity = 65536;

using CsvRing = statusbar::itc::QueuedPipe<UdpTunCsvRecord, csv_ring_capacity>;

/// Default colbin pre-allocation size, for the reference recording use case:
/// a 15-minute capture at 96 kHz. The tunnel packs 44 frames/packet, so
/// 96000/44 ≈ 2182 packets/s per stream; with stream redundancy the recorder
/// logs ~2× that ≈ 4364 rows/s. Over 900 s that is ≈ 3.93 M rows × 48 B
/// ≈ 180 MiB; rounded up to 256 MiB for headroom (slightly longer runs, slow
/// drains). The colbin Writer pre-allocates this in full and never grows, so no
/// mid-run mremap stalls the real-time data plane.
inline constexpr uint64_t default_colbin_capacity_bytes = 256ULL * 1024 * 1024;

/// Canonical CSV header (kept in lock-step with format_csv_record's output order).
inline constexpr std::array<std::string_view, 7> kUdpTunCsvHeader = {
    "rx_gptp_ns", "presentation_time_ns", "latency_ns", "sender_eui64", "sequence", "interval_us", "role"};

/// Canonical colbin schema (matches UdpTunCsvRecord's exact 48-byte
/// layout: i64×3, u64, u32×2, u8 + 7 bytes pad — naturally aligned).
/// The .colbin row is just a memcpy of the record struct.
[[nodiscard]] inline auto udptun_colbin_schema() -> std::vector<statusbar::colbin::ColumnSpec>
{
    namespace cb = statusbar::colbin;
    return {
        {.name = "rx_gptp_ns", .type = cb::TypeCode::i64},
        {.name = "presentation_time_ns", .type = cb::TypeCode::i64},
        {.name = "latency_ns", .type = cb::TypeCode::i64},
        {.name = "sender_eui64", .type = cb::TypeCode::u64},
        {.name = "sequence", .type = cb::TypeCode::u32},
        {.name = "interval_us", .type = cb::TypeCode::u32},
        {.name = "role", .type = cb::TypeCode::u8},
    };
}

/// Caller-owned scratch buffers for record stringification.
/// Sized so std::snprintf cannot overflow any one field.
struct CsvScratch
{
    std::array<char, 24> rx_gptp;
    std::array<char, 24> pt;
    std::array<char, 24> latency;
    std::array<char, 24> eui64;  // 23 chars + NUL — exactly sized for %02x:...:%02x
    std::array<char, 16> seq;
    std::array<char, 16> interval;
};

/// Stringify `rec` into the seven fields expected by CsvWriter::write_row.
/// The `role` slot is filled by `role_to_string` from a string-literal
/// table (no scratch needed); the other six slots use snprintf into
/// caller-owned `scratch`.
void format_csv_record(UdpTunCsvRecord const& rec, CsvScratch& scratch, std::array<std::string_view, 7>& out);

/// Stable lowercase-underscore string for each PacketRole enumerator.
[[nodiscard]] auto role_to_string(PacketRole role) noexcept -> std::string_view;

/// Optional CSV sink fed into RxContext. When `ring == nullptr` (the
/// default — CSV disabled), `append` is a no-op. Otherwise records are
/// non-blocking-published into the SPSC ring; the Session owns a
/// dedicated writer thread on the consuming end that streams them out
/// to disk. If the ring is full (writer thread stalled), the record is
/// dropped and counted via `records_dropped`.
struct CsvSink
{
    CsvRing* ring{nullptr};
    statusbar::itc::TelemetryCounter<uint64_t>* records_dropped{nullptr};

    void append(UdpTunCsvRecord const& rec) const noexcept
    {
        if (ring == nullptr) {
            return;
        }
        if (!ring->try_publish(rec)) {
            if (records_dropped != nullptr) {
                records_dropped->add();
            }
        }
    }
};

}  // namespace statusbar::udptun
