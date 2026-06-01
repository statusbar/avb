#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/atdecc/atdecc_format.hpp"
#include "statusbar/avtp/avtp_format.hpp"
#include "statusbar/gptp/gptp_format.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/ip/ip_format.hpp"
#include "statusbar/srp/srp_format.hpp"
#include "statusbar/tsn/tsn_format.hpp"

#include <cstdint>
#include <format>
#include <iterator>
#include <span>
#include <string>

namespace statusbar::netdump::detail {

/// Format a hex dump to an output iterator
template <typename OutputIt>
auto format_hex_dump(OutputIt out, std::span<uint8_t const> data) -> OutputIt
{
    for (auto const octet_value : data) {
        out = std::format_to(out, "{:02x} ", octet_value);
    }
    out = std::format_to(out, "\n");
    return out;
}

/// Format a structure with leading indent to an output iterator
template <typename OutputIt, typename T>
auto format_item(OutputIt out, T const& item) -> OutputIt
{
    out = std::format_to(out, "  ");
    out = format_to(out, item);
    out = std::format_to(out, "\n");
    return out;
}

}  // namespace statusbar::netdump::detail

namespace statusbar::netdump {

/// Format an ICMP payload to an output iterator
template <typename OutputIt>
auto format_icmp(OutputIt out, std::span<uint8_t const> ip_payload) -> OutputIt
{
    using namespace ip;

    if (ip_payload.size() < IcmpHeader::LENGTH) {
        out = std::format_to(out, "  ICMP header truncated\n");
        return detail::format_hex_dump(out, ip_payload);
    }

    IcmpHeader icmp_hdr;
    (void)load_unchecked(ip_payload, &icmp_hdr);
    out = detail::format_item(out, icmp_hdr);

    auto const icmp_payload = ip_payload.subspan(IcmpHeader::LENGTH);
    if (!icmp_payload.empty()) {
        out = std::format_to(out, "  ICMP payload ({} bytes): ", icmp_payload.size());
        out = detail::format_hex_dump(out, icmp_payload);
    }
    return out;
}

/// Format an IGMP payload to an output iterator
template <typename OutputIt>
auto format_igmp(OutputIt out, std::span<uint8_t const> ip_payload) -> OutputIt
{
    using namespace ip;

    if (ip_payload.size() < IgmpHeader::LENGTH) {
        out = std::format_to(out, "  IGMP header truncated\n");
        return detail::format_hex_dump(out, ip_payload);
    }

    IgmpHeader igmp_hdr;
    (void)load_unchecked(ip_payload, &igmp_hdr);
    out = detail::format_item(out, igmp_hdr);
    return out;
}

/// Format a UDP payload to an output iterator
template <typename OutputIt>
auto format_udp(OutputIt out, std::span<uint8_t const> ip_payload) -> OutputIt
{
    using namespace ip;

    if (ip_payload.size() < UdpHeader::LENGTH) {
        out = std::format_to(out, "  UDP header truncated\n");
        return detail::format_hex_dump(out, ip_payload);
    }

    UdpHeader udp_hdr;
    (void)load_unchecked(ip_payload, &udp_hdr);
    out = detail::format_item(out, udp_hdr);

    auto const udp_payload = ip_payload.subspan(UdpHeader::LENGTH);
    if (!udp_payload.empty()) {
        out = std::format_to(out, "  UDP payload ({} bytes): ", udp_payload.size());
        out = detail::format_hex_dump(out, udp_payload);
    }
    return out;
}

/// Format an ARP payload to an output iterator
template <typename OutputIt>
auto format_arp(OutputIt out, std::span<uint8_t const> payload) -> OutputIt
{
    using namespace ip;

    if (payload.size() < ArpHeader::LENGTH) {
        out = std::format_to(out, "  ARP header truncated\n");
        return detail::format_hex_dump(out, payload);
    }

    ArpHeader arp_hdr;
    (void)load_unchecked(payload, &arp_hdr);
    out = detail::format_item(out, arp_hdr);
    return out;
}

/// Format an IPv4 packet to an output iterator
template <typename OutputIt>
auto format_ipv4(OutputIt out, std::span<uint8_t const> payload) -> OutputIt
{
    using namespace ip;

    if (payload.size() < IPv4Header::LENGTH) {
        out = std::format_to(out, "  IPv4 header truncated\n");
        return detail::format_hex_dump(out, payload);
    }

    IPv4Header ip_hdr;
    (void)load_unchecked(payload, &ip_hdr);
    out = detail::format_item(out, ip_hdr);

    auto const hdr_len = ip_hdr.header_length();
    if (hdr_len < IPv4Header::LENGTH || hdr_len > payload.size()) {
        out = std::format_to(out, "  IPv4 invalid header length {} (payload {} bytes)\n", hdr_len, payload.size());
        return detail::format_hex_dump(out, payload);
    }
    auto const ip_payload = payload.subspan(hdr_len);

    switch (ip_hdr.protocol.get()) {
        case IPv4Header::PROTOCOL_ICMP:
            out = format_icmp(out, ip_payload);
            break;
        case IPv4Header::PROTOCOL_IGMP:
            out = format_igmp(out, ip_payload);
            break;
        case IPv4Header::PROTOCOL_UDP:
            out = format_udp(out, ip_payload);
            break;
        default:
            out = std::format_to(out, "  IP payload ({} bytes): ", ip_payload.size());
            out = detail::format_hex_dump(out, ip_payload);
            break;
    }
    return out;
}

/// Format an IPv6 packet to an output iterator
template <typename OutputIt>
auto format_ipv6(OutputIt out, std::span<uint8_t const> payload) -> OutputIt
{
    using namespace ip;

    if (payload.size() < IPv6Header::LENGTH) {
        out = std::format_to(out, "  IPv6 header truncated\n");
        return detail::format_hex_dump(out, payload);
    }

    IPv6Header ip_hdr;
    (void)load_unchecked(payload, &ip_hdr);
    out = detail::format_item(out, ip_hdr);

    auto const ip_payload = payload.subspan(IPv6Header::LENGTH);

    switch (ip_hdr.next_header.get()) {
        case IPv6Header::NEXT_HEADER_ICMPV6:
            out = ip::format_icmpv6(out, ip_payload);
            break;
        case IPv6Header::NEXT_HEADER_UDP:
            out = format_udp(out, ip_payload);
            break;
        default:
            out = std::format_to(out, "  IPv6 payload ({} bytes): ", ip_payload.size());
            out = detail::format_hex_dump(out, ip_payload);
            break;
    }
    return out;
}

/// Format a gPTP packet to an output iterator
template <typename OutputIt>
auto format_gptp(OutputIt out, std::span<uint8_t const> payload) -> OutputIt
{
    using namespace gptp;

    auto const msg = parse_gptp(payload);
    if (!msg) {
        out = std::format_to(out, "  gPTP header truncated\n");
        return detail::format_hex_dump(out, payload);
    }
    out = detail::format_item(out, *msg);
    return out;
}

/// Format an AVTP EtherType payload to an output iterator
/// Dispatches to stream data (AM824, AAF) or control (ATDECC) based on subtype
template <typename OutputIt>
auto format_avtp_ethertype(OutputIt out, std::span<uint8_t const> payload) -> OutputIt
{
    if (payload.empty()) {
        return std::format_to(out, "  AVTP payload empty\n");
    }

    // Extract subtype from first byte
    // Subtypes < 0xFA are stream data (AM824, AAF, CVF, CRF, etc.)
    // Subtypes >= 0xFA are control protocols (MAAP, ADP, ACMP, AECP)
    uint8_t const subtype = payload[0];

    if (subtype < 0xFAU) {
        // Stream data format - dispatch to AVTP module
        return avtp::format_avtp(out, payload);
    }
    // Control protocol - dispatch to ATDECC module
    return atdecc::format_atdecc(out, payload);
}

/// Format an Ethernet frame to an output iterator
template <typename OutputIt>
auto format_frame(OutputIt out, std::span<uint8_t const> frame) -> OutputIt
{
    using namespace ieee;
    using namespace ieee::protocols;

    // can_load(EthernetFrame*) returns 14 for an untagged frame, 18 for an
    // 802.1Q-tagged frame (TPID 0x8100), and an error if the buffer is
    // shorter than the size implied by its TPID. A bare `frame.size() < 14`
    // check is not enough: a 16-byte frame whose ethertype-position bytes
    // are 0x8100 would slip past it and then `load_unchecked` would read
    // the next 4 bytes (vlan_tag + inner ethertype) past the end of the
    // buffer.
    EthernetFrame eth;
    auto const required = can_load(frame, &eth);
    if (!required) {
        out = std::format_to(out, "  Frame too short ({} bytes)\n", frame.size());
        return detail::format_hex_dump(out, frame);
    }

    auto const eth_size = load_unchecked(frame, &eth);

    out = detail::format_item(out, eth);

    auto const payload = frame.subspan(eth_size);
    uint16_t const ethertype = eth.ethertype;

    switch (ethertype) {
        case ETHERTYPE_ARP:
            out = format_arp(out, payload);
            break;
        case ETHERTYPE_IPV4:
            out = format_ipv4(out, payload);
            break;
        case ETHERTYPE_IPV6:
            out = format_ipv6(out, payload);
            break;
        case ETHERTYPE_MSRP:
            out = statusbar::srp::msrp::format_msrp(out, payload);
            break;
        case ETHERTYPE_MVRP:
            out = statusbar::srp::mvrp::format_mvrp(out, payload);
            break;
        case ETHERTYPE_AVTP:
            out = format_avtp_ethertype(out, payload);
            break;
        case gptp::GPTP_ETHERTYPE:
            out = format_gptp(out, payload);
            break;
        default:
            out = std::format_to(out, "  Ethernet payload ({} bytes): ", payload.size());
            out = detail::format_hex_dump(out, payload);
            break;
    }

    return out;
}

}  // namespace statusbar::netdump
