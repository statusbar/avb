// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_csv_record.hpp"

#include <cstdio>
#include <span>
#include <type_traits>

namespace statusbar::udptun {

auto role_to_string(PacketRole role) noexcept -> std::string_view
{
    switch (role) {
        case PacketRole::SelfPrimary:
            return "self_primary";
        case PacketRole::SelfRedundant:
            return "self_redundant";
        case PacketRole::SelfLegacy:
            return "self_legacy";
        case PacketRole::RemotePrimary:
            return "remote_primary";
        case PacketRole::RemoteRedundant:
            return "remote_redundant";
        case PacketRole::RemoteLegacy:
            return "remote_legacy";
    }
    return "unknown";
}

namespace {

template <typename T>
[[nodiscard]] auto format_int(std::span<char> buf, T value) -> std::string_view
{
    // %lld for signed, %llu for unsigned — branch on signedness.
    int n = 0;
    if constexpr (std::is_signed_v<T>) {
        n = std::snprintf(buf.data(), buf.size(), "%lld", static_cast<long long>(value));
    } else {
        n = std::snprintf(buf.data(), buf.size(), "%llu", static_cast<unsigned long long>(value));
    }
    return std::string_view{buf.data(), static_cast<size_t>(n > 0 ? n : 0)};
}

[[nodiscard]] auto format_eui64(std::span<char> buf, ieee::Eui64 const& eui) -> std::string_view
{
    auto const s = eui.span();
    int const n = std::snprintf(
        buf.data(), buf.size(), "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x", s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
    return std::string_view{buf.data(), static_cast<size_t>(n > 0 ? n : 0)};
}

}  // namespace

void format_csv_record(UdpTunCsvRecord const& rec, CsvScratch& scratch, std::array<std::string_view, 7>& out)
{
    out[0] = format_int(std::span<char>{scratch.rx_gptp}, rec.rx_gptp_ns);
    out[1] = format_int(std::span<char>{scratch.pt}, rec.presentation_time_ns);
    out[2] = format_int(std::span<char>{scratch.latency}, rec.latency_ns);
    out[3] = format_eui64(std::span<char>{scratch.eui64}, rec.sender_id);
    out[4] = format_int(std::span<char>{scratch.seq}, rec.sequence);
    out[5] = format_int(std::span<char>{scratch.interval}, rec.interval_us);
    out[6] = role_to_string(static_cast<PacketRole>(rec.role));
}

}  // namespace statusbar::udptun
