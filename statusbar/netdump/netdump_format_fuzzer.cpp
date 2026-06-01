// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for netdump::format_frame. This is the entry-point
/// formatter for raw Ethernet frames coming off the wire (pcap, BPF,
/// XDP). format_frame dispatches by EtherType into:
///   - format_arp
///   - format_ipv4 (which dispatches by IP protocol to ICMP/IGMP/UDP/...)
///   - format_ipv6
///   - format_msrp / format_mvrp (SRP attribute decoders)
///   - format_avtp_ethertype (which dispatches by AVTP subtype)
/// A single harness covers every byte-parser the dispatcher reaches.

#include "statusbar/netdump/netdump_format.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const frame = std::span<uint8_t const>(data, size);

    // format_frame is a template parameterised on the output iterator type.
    // Use a back_inserter into a local vector to discard the formatted text
    // while still exercising every byte the parsers touch.
    std::vector<char> sink;
    sink.reserve(1024);
    statusbar::netdump::format_frame(std::back_inserter(sink), frame);
    return 0;
}
