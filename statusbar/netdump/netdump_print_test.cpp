// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/netdump/netdump.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>

using namespace statusbar;
using namespace statusbar::netdump;
using namespace statusbar::ieee;
using namespace statusbar::ip;
using namespace statusbar::gptp;

// ===================================================================
// Helper: build an untagged Ethernet frame in a byte buffer
// Returns the total number of bytes written (14 + payload_size)
// ===================================================================
static auto build_ethernet_frame(
    std::span<uint8_t> buf, Eui48 const& dst, Eui48 const& src, uint16_t ethertype, std::span<uint8_t const> payload) -> size_t
{
    EthernetFrame eth;
    eth.dest_mac = dst;
    eth.src_mac = src;
    eth.vlan_tag = VlanTag{};  // no VLAN
    eth.ethertype = ethertype;
    auto const hdr_size = store_unchecked(buf, eth);
    // Copy payload after header
    span_copy(std::span<uint8_t>(buf).subspan(hdr_size, payload.size()), payload);
    return hdr_size + payload.size();
}

// ===================================================================
// Helper: build a minimal IPv4 header in a byte buffer
// Returns number of bytes written (20)
// ===================================================================
static auto build_ipv4_header(std::span<uint8_t> buf, uint8_t protocol, uint16_t total_length) -> size_t
{
    IPv4Header hdr{};
    hdr.set_version(4);
    hdr.set_ihl(5);  // 20 bytes, no options
    hdr.total_length = total_length;
    hdr.ttl = 64;
    hdr.protocol = protocol;
    hdr.src_addr = IPv4Address{10, 0, 0, 1};
    hdr.dst_addr = IPv4Address{10, 0, 0, 2};
    return ip::store_unchecked(buf, hdr);
}

// ===================================================================
// Tests: detail::format_hex_dump
// ===================================================================

TEST(netdump_hex_dump, empty_data)
{
    std::string result;
    netdump::detail::format_hex_dump(std::back_inserter(result), std::span<uint8_t const>{});
    // Should output just a newline for empty data
    EXPECT_EQ(result, "\n");
}

TEST(netdump_hex_dump, single_byte)
{
    std::array<uint8_t, 1> data{0xAB};
    std::string result;
    netdump::detail::format_hex_dump(std::back_inserter(result), std::span<uint8_t const>{data});
    EXPECT_EQ(result, "ab \n");
}

TEST(netdump_hex_dump, multiple_bytes)
{
    std::array<uint8_t, 4> data{0x01, 0x23, 0x45, 0x67};
    std::string result;
    netdump::detail::format_hex_dump(std::back_inserter(result), std::span<uint8_t const>{data});
    EXPECT_EQ(result, "01 23 45 67 \n");
}

// ===================================================================
// Tests: detail::format_item
// ===================================================================

TEST(netdump_format_item, ipv4_header)
{
    // Build and format a simple IPv4 header
    std::array<uint8_t, 20> buf{};
    build_ipv4_header(buf, IPv4Header::PROTOCOL_UDP, 28);

    IPv4Header hdr{};
    (void)ip::load_unchecked(std::span<uint8_t const>{buf}, &hdr);

    std::string result;
    netdump::detail::format_item(std::back_inserter(result), hdr);

    // Should start with indent and end with newline
    EXPECT_TRUE(result.starts_with("  "));
    EXPECT_TRUE(result.ends_with("\n"));

    // Should contain the IP addresses
    EXPECT_TRUE(result.find("10.0.0.1") != std::string::npos);
    EXPECT_TRUE(result.find("10.0.0.2") != std::string::npos);
}

// ===================================================================
// Tests: format_ipv4
// ===================================================================

TEST(netdump_format_ipv4, valid_udp_packet)
{
    // Build IPv4 + UDP header
    std::array<uint8_t, 128> buf{};
    auto const ip_size = build_ipv4_header(buf, IPv4Header::PROTOCOL_UDP, 28);

    // Build a minimal UDP header after the IP header
    UdpHeader udp{};
    udp.src_port = 5004;
    udp.dst_port = 5005;
    udp.length = 8;
    udp.checksum = 0;
    (void)ip::store_unchecked(std::span{buf}.subspan(ip_size), udp);

    std::string result;
    format_ipv4(std::back_inserter(result), std::span<uint8_t const>{buf}.first(ip_size + UdpHeader::LENGTH));

    // Should contain IPv4 info and UDP info
    EXPECT_TRUE(result.find("10.0.0.1") != std::string::npos);
    EXPECT_TRUE(result.find("10.0.0.2") != std::string::npos);
}

TEST(netdump_format_ipv4, valid_icmp_packet)
{
    std::array<uint8_t, 128> buf{};
    auto const ip_size = build_ipv4_header(buf, IPv4Header::PROTOCOL_ICMP, 28);

    // Build a minimal ICMP header
    IcmpHeader icmp{};
    icmp.type = 8;  // Echo request
    icmp.code = 0;
    (void)ip::store_unchecked(std::span{buf}.subspan(ip_size), icmp);

    std::string result;
    format_ipv4(std::back_inserter(result), std::span<uint8_t const>{buf}.first(ip_size + IcmpHeader::LENGTH));

    EXPECT_TRUE(result.find("10.0.0.1") != std::string::npos);
}

TEST(netdump_format_ipv4, valid_igmp_packet)
{
    std::array<uint8_t, 128> buf{};
    auto const ip_size = build_ipv4_header(buf, IPv4Header::PROTOCOL_IGMP, 28);

    IgmpHeader igmp{};
    (void)ip::store_unchecked(std::span{buf}.subspan(ip_size), igmp);

    std::string result;
    format_ipv4(std::back_inserter(result), std::span<uint8_t const>{buf}.first(ip_size + IgmpHeader::LENGTH));

    EXPECT_TRUE(result.find("10.0.0.1") != std::string::npos);
}

TEST(netdump_format_ipv4, unknown_protocol)
{
    std::array<uint8_t, 128> buf{};
    auto const ip_size = build_ipv4_header(buf, 99, 24);  // Unknown protocol

    // Put some payload bytes
    buf[ip_size] = 0xDE;
    buf[ip_size + 1] = 0xAD;
    buf[ip_size + 2] = 0xBE;
    buf[ip_size + 3] = 0xEF;

    std::string result;
    format_ipv4(std::back_inserter(result), std::span<uint8_t const>{buf}.first(ip_size + 4));

    // Should contain "IP payload" text and hex dump
    EXPECT_TRUE(result.find("IP payload") != std::string::npos);
    EXPECT_TRUE(result.find("de") != std::string::npos);
}

// ===================================================================
// Tests: format_ipv6
// ===================================================================

TEST(netdump_format_ipv6, valid_udp_packet)
{
    std::array<uint8_t, 128> buf{};

    // Build a minimal IPv6 header
    IPv6Header hdr{};
    hdr.set_version(6);
    hdr.payload_length = UdpHeader::LENGTH;
    hdr.next_header = IPv6Header::NEXT_HEADER_UDP;
    hdr.hop_limit = 64;
    auto const ip_size = ip::store_unchecked(std::span<uint8_t>{buf}, hdr);

    // Build UDP header after IPv6
    UdpHeader udp{};
    udp.src_port = 5004;
    udp.dst_port = 5005;
    udp.length = UdpHeader::LENGTH;
    (void)ip::store_unchecked(std::span{buf}.subspan(ip_size), udp);

    std::string result;
    format_ipv6(std::back_inserter(result), std::span<uint8_t const>{buf}.first(ip_size + UdpHeader::LENGTH));

    // Should produce output without errors
    EXPECT_FALSE(result.empty());
    // Should not report truncation
    EXPECT_TRUE(result.find("truncated") == std::string::npos);
}

TEST(netdump_format_ipv6, unknown_next_header)
{
    std::array<uint8_t, 128> buf{};

    IPv6Header hdr{};
    hdr.set_version(6);
    hdr.payload_length = 4;
    hdr.next_header = 99;  // Unknown
    hdr.hop_limit = 64;
    auto const ip_size = ip::store_unchecked(std::span<uint8_t>{buf}, hdr);

    buf[ip_size] = 0xCA;
    buf[ip_size + 1] = 0xFE;

    std::string result;
    format_ipv6(std::back_inserter(result), std::span<uint8_t const>{buf}.first(ip_size + 2));

    EXPECT_TRUE(result.find("IPv6 payload") != std::string::npos);
}

// ===================================================================
// Tests: format_avtp_ethertype
// ===================================================================

TEST(netdump_format_avtp, empty_payload)
{
    std::string result;
    format_avtp_ethertype(std::back_inserter(result), std::span<uint8_t const>{});
    EXPECT_TRUE(result.find("empty") != std::string::npos);
}

TEST(netdump_format_avtp, stream_data_subtype)
{
    // Subtype < 0xFA is stream data - use AAF subtype (0x02)
    std::array<uint8_t, 64> buf{};
    buf[0] = 0x02;  // AAF subtype

    std::string result;
    format_avtp_ethertype(std::back_inserter(result), std::span<uint8_t const>{buf});

    // Should produce output (dispatched to avtp::format_avtp)
    EXPECT_FALSE(result.empty());
}

TEST(netdump_format_avtp, control_subtype)
{
    // Subtype >= 0xFA is control (ATDECC) - use ADP subtype (0xFA)
    std::array<uint8_t, 64> buf{};
    buf[0] = 0xFA;  // ADP subtype

    std::string result;
    format_avtp_ethertype(std::back_inserter(result), std::span<uint8_t const>{buf});

    // Should produce output (dispatched to atdecc::format_atdecc)
    EXPECT_FALSE(result.empty());
}

// ===================================================================
// Tests: format_gptp
// ===================================================================

TEST(netdump_format_gptp, valid_sync_message)
{
    // Build a valid gPTP Sync message
    SyncMessage sync;
    sync.header.init(MESSAGE_TYPE_SYNC, SYNC_MESSAGE_LENGTH, 100);

    std::array<uint8_t, 128> buf{};
    auto const stored = store_unchecked(std::span{buf}.first(SYNC_MESSAGE_LENGTH), sync);
    EXPECT_EQ(stored, static_cast<size_t>(SYNC_MESSAGE_LENGTH));

    std::string result;
    format_gptp(std::back_inserter(result), std::span<uint8_t const>{buf}.first(SYNC_MESSAGE_LENGTH));

    // Should produce formatted output without truncation warning
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.find("truncated") == std::string::npos);
}

TEST(netdump_format_gptp, truncated_message)
{
    // Too short for a gPTP header
    std::array<uint8_t, 4> buf{};
    std::string result;
    format_gptp(std::back_inserter(result), std::span<uint8_t const>{buf});

    EXPECT_TRUE(result.find("truncated") != std::string::npos);
}

// ===================================================================
// Tests: format_frame (top-level EtherType dispatch)
// ===================================================================

TEST(netdump_format_frame, ipv4_frame)
{
    // Build an Ethernet frame with IPv4 payload
    std::array<uint8_t, 128> ipv4_buf{};
    auto const ip_size = build_ipv4_header(ipv4_buf, IPv4Header::PROTOCOL_UDP, 28);

    UdpHeader udp{};
    udp.src_port = 5004;
    udp.dst_port = 5005;
    udp.length = UdpHeader::LENGTH;
    (void)ip::store_unchecked(std::span{ipv4_buf}.subspan(ip_size), udp);
    auto const payload_size = ip_size + UdpHeader::LENGTH;

    std::array<uint8_t, 256> frame{};
    auto const frame_size = build_ethernet_frame(
        frame,
        Eui48{0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
        Eui48{0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F},
        ieee::protocols::ETHERTYPE_IPV4,
        std::span<uint8_t const>{ipv4_buf}.first(payload_size));

    std::string result;
    format_frame(std::back_inserter(result), std::span<uint8_t const>{frame}.first(frame_size));

    // Should contain MAC addresses and IP info
    EXPECT_TRUE(result.find("10.0.0.1") != std::string::npos);
    EXPECT_TRUE(result.find("10.0.0.2") != std::string::npos);
}

TEST(netdump_format_frame, arp_frame)
{
    // Build an ARP payload
    std::array<uint8_t, 64> arp_buf{};
    ArpHeader arp{};  // Default constructor sets hardware_type, protocol_type, operation
    (void)ip::store_unchecked(std::span<uint8_t>{arp_buf}, arp);

    std::array<uint8_t, 256> frame{};
    auto const frame_size = build_ethernet_frame(
        frame,
        Eui48{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
        Eui48{0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F},
        ieee::protocols::ETHERTYPE_ARP,
        std::span<uint8_t const>{arp_buf}.first(ArpHeader::LENGTH));

    std::string result;
    format_frame(std::back_inserter(result), std::span<uint8_t const>{frame}.first(frame_size));

    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.find("truncated") == std::string::npos);
}

TEST(netdump_format_frame, unknown_ethertype)
{
    std::array<uint8_t, 4> payload{0xDE, 0xAD, 0xBE, 0xEF};

    std::array<uint8_t, 256> frame{};
    auto const frame_size = build_ethernet_frame(
        frame,
        Eui48{0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
        Eui48{0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F},
        0x9999,  // Unknown EtherType
        std::span<uint8_t const>{payload});

    std::string result;
    format_frame(std::back_inserter(result), std::span<uint8_t const>{frame}.first(frame_size));

    // Should contain "Ethernet payload" for unknown ethertype
    EXPECT_TRUE(result.find("Ethernet payload") != std::string::npos);
    EXPECT_TRUE(result.find("de") != std::string::npos);
}

TEST(netdump_format_frame, gptp_frame)
{
    // Build a gPTP Sync message as payload
    SyncMessage sync;
    sync.header.init(MESSAGE_TYPE_SYNC, SYNC_MESSAGE_LENGTH, 100);

    std::array<uint8_t, 128> gptp_buf{};
    (void)store_unchecked(std::span{gptp_buf}.first(SYNC_MESSAGE_LENGTH), sync);

    std::array<uint8_t, 256> frame{};
    auto const frame_size = build_ethernet_frame(
        frame,
        Eui48{0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E},  // gPTP multicast
        Eui48{0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F},
        GPTP_ETHERTYPE,
        std::span<uint8_t const>{gptp_buf}.first(SYNC_MESSAGE_LENGTH));

    std::string result;
    format_frame(std::back_inserter(result), std::span<uint8_t const>{frame}.first(frame_size));

    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.find("truncated") == std::string::npos);
}

TEST(netdump_format_frame, avtp_frame)
{
    // Build a minimal AVTP stream data payload (AAF subtype = 0x02)
    std::array<uint8_t, 64> avtp_buf{};
    avtp_buf[0] = 0x02;  // AAF subtype

    std::array<uint8_t, 256> frame{};
    auto const frame_size = build_ethernet_frame(
        frame,
        Eui48{0x91, 0xE0, 0xF0, 0x00, 0x00, 0x01},
        Eui48{0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F},
        ieee::protocols::ETHERTYPE_AVTP,
        std::span<uint8_t const>{avtp_buf});

    std::string result;
    format_frame(std::back_inserter(result), std::span<uint8_t const>{frame}.first(frame_size));

    EXPECT_FALSE(result.empty());
}

// ===================================================================
// Tests: truncated inputs / graceful degradation
// ===================================================================

TEST(netdump_truncated, frame_too_short)
{
    std::array<uint8_t, 6> buf{0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    std::string result;
    format_frame(std::back_inserter(result), std::span<uint8_t const>{buf});

    EXPECT_TRUE(result.find("too short") != std::string::npos);
}

TEST(netdump_truncated, ipv4_header_truncated)
{
    // Less than 20 bytes for IPv4
    std::array<uint8_t, 10> buf{};
    buf[0] = 0x45;  // version=4, ihl=5

    std::string result;
    format_ipv4(std::back_inserter(result), std::span<uint8_t const>{buf});

    EXPECT_TRUE(result.find("truncated") != std::string::npos);
}

TEST(netdump_truncated, ipv6_header_truncated)
{
    // Less than 40 bytes for IPv6
    std::array<uint8_t, 10> buf{};

    std::string result;
    format_ipv6(std::back_inserter(result), std::span<uint8_t const>{buf});

    EXPECT_TRUE(result.find("truncated") != std::string::npos);
}

TEST(netdump_truncated, udp_header_truncated)
{
    // Less than 8 bytes for UDP
    std::array<uint8_t, 4> buf{};

    std::string result;
    format_udp(std::back_inserter(result), std::span<uint8_t const>{buf});

    EXPECT_TRUE(result.find("truncated") != std::string::npos);
}

TEST(netdump_truncated, icmp_header_truncated)
{
    std::array<uint8_t, 4> buf{};

    std::string result;
    format_icmp(std::back_inserter(result), std::span<uint8_t const>{buf});

    EXPECT_TRUE(result.find("truncated") != std::string::npos);
}

TEST(netdump_truncated, igmp_header_truncated)
{
    std::array<uint8_t, 4> buf{};

    std::string result;
    format_igmp(std::back_inserter(result), std::span<uint8_t const>{buf});

    EXPECT_TRUE(result.find("truncated") != std::string::npos);
}

TEST(netdump_truncated, arp_header_truncated)
{
    std::array<uint8_t, 10> buf{};

    std::string result;
    format_arp(std::back_inserter(result), std::span<uint8_t const>{buf});

    EXPECT_TRUE(result.find("truncated") != std::string::npos);
}

// Main test runner function required by create_test_sourcelist
TEST_MAIN(statusbar_netdump, netdump_print_test)
