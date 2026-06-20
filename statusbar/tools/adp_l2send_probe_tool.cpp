// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT
//
// statusbar-adp-l2send-probe — minimal, self-contained L2 ADPDU send probe.
//
// Purpose: isolate why an AF_PACKET frame sent with PACKET_QDISC_BYPASS appears
// to be dropped in the driver before it reaches the wire / tcpdump on the same
// Pi. This tool intentionally uses ONLY raw POSIX syscalls (no RawnetContext,
// no statusbar net stack) so that, if it reproduces the drop, the problem is in
// the kernel/driver/NIC config — not in our library code.
//
// It builds a real 68-byte ENTITY_AVAILABLE ADPDU (EtherType 0x22F0, dest =
// ATDECC multicast 91:E0:F0:01:00:00), sends it N times, and for each run
// reports:
//   - whether setsockopt(PACKET_QDISC_BYPASS) actually succeeded (read back),
//   - the exact frame length on the wire (runt check: < 60 bytes),
//   - sendto() return value and errno per frame,
//   - the driver's own counters from /sys/class/net/<iface>/statistics
//     (tx_packets / tx_bytes / tx_dropped / tx_errors / tx_carrier_errors /
//     tx_fifo_errors) sampled before and after the burst.
//
// The decisive signal: if sendto() succeeds K times but tx_packets only rises
// by < K (or tx_dropped/tx_errors rise), the driver is dropping the frame after
// accepting it from the socket — a driver/NIC issue, not our code. If
// tx_packets rises by K but tcpdump still sees nothing, the loss is past the
// driver counter (offload/hardware). Compare --bypass=0 vs --bypass=1 (or use
// --compare to run both back-to-back) to attribute the loss to QDISC_BYPASS.
//
// Usage:
//   statusbar-adp-l2send-probe --interface=eth0
//   statusbar-adp-l2send-probe --interface=eth0 --compare
//   statusbar-adp-l2send-probe --interface=eth0 --bypass=1 --count=10 --pad=0
//
// Run tcpdump in another shell to see what (if anything) reaches capture:
//   sudo tcpdump -i eth0 -e -nn ether proto 0x22f0
//
// Needs CAP_NET_RAW (run as root or `setcap cap_net_raw,cap_net_admin+ep`).

#include "statusbar/config/config.hpp"
#include "statusbar/ieee/ieee_ethernet.hpp"
#include "statusbar/status/throw_or_abort.hpp"

#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>

namespace {

constexpr uint16_t ETHERTYPE_AVTP = 0x22F0;
constexpr size_t ETH_HDR = 14;
constexpr size_t ADPDU_LEN = 68;        // ENTITY_AVAILABLE ADPDU
constexpr size_t MIN_ETH_PAYLOAD = 60;  // min L2 frame w/o FCS; below = runt
constexpr std::array<uint8_t, 6> ADP_MCAST{0x91, 0xE0, 0xF0, 0x01, 0x00, 0x00};

struct Config
{
    std::string interface;
    int bypass = 1;                                 // PACKET_QDISC_BYPASS on/off
    int bind_sock = 0;                              // bind() to the iface before sending
    int count = 5;                                  // frames per run
    int interval_ms = 200;                          // gap between frames
    int pad = 1;                                    // pad short frames up to 60 bytes
    int payload_len = static_cast<int>(ADPDU_LEN);  // override ADPDU bytes sent
    std::array<uint8_t, 6> dest = ADP_MCAST;
    uint16_t ethertype = ETHERTYPE_AVTP;
    bool compare = false;  // run bypass=0 then bypass=1
    bool verbose = false;
    // Receiver mode: raw-capture frames of --ethertype, count those from --from.
    bool listen = false;
    int listen_secs = 10;
    std::array<uint8_t, 6> from{};
    bool from_set = false;
};

// Parse a MAC into the raw 6-byte array via the native ieee parser.
auto parse_mac(std::string_view s, std::array<uint8_t, 6>& out) -> bool
{
    auto const e = statusbar::ieee::eui48_from_string(s);
    if (!e) {
        return false;
    }
    std::memcpy(out.data(), e->span().data(), out.size());
    return true;
}

auto build_arg_specs(Config& c) -> statusbar::args::ArgumentSpecs
{
    statusbar::args::ArgumentSpecs specs;
    specs.add_device("interface", "Network interface (e.g. eth0) [required]", "", [&](auto v) { c.interface = std::string{v}; });
    specs.add<int>("bypass", "Set PACKET_QDISC_BYPASS (0|1)", c.bypass, [&](auto v) { c.bypass = v; });
    specs.add<int>("bind", "bind() socket to iface first (0|1)", c.bind_sock, [&](auto v) { c.bind_sock = v; });
    specs.add<int>("count", "Frames to send per run", c.count, [&](auto v) { c.count = v; });
    specs.add<int>("interval-ms", "Gap between frames (ms)", c.interval_ms, [&](auto v) { c.interval_ms = v; });
    specs.add<int>("pad", "Pad frame to 60 bytes / runt fix (0|1)", c.pad, [&](auto v) { c.pad = v; });
    specs.add<int>("payload-len", "ADPDU bytes to send (runt test)", c.payload_len, [&](auto v) { c.payload_len = v; });
    specs.add<std::string>("dest", "Destination MAC aa:bb:.. (default ATDECC mcast)", "", [&](auto v) {
        if (!v.empty() && !parse_mac(v, c.dest)) {
            statusbar::throw_or_abort(std::errc::invalid_argument, "invalid --dest MAC");
        }
    });
    specs.add<std::string>("ethertype", "EtherType (0xNNNN)", "0x22f0", [&](auto v) {
        c.ethertype = static_cast<uint16_t>(std::strtoul(std::string{v}.c_str(), nullptr, 0));
    });
    specs.add_flag("compare", "Run bypass=0 then bypass=1 back-to-back", [&](auto v) { c.compare = v; });
    specs.add_flag("verbose", "Hexdump the frame", [&](auto v) { c.verbose = v; });
    specs.add_flag("listen", "RECEIVER mode: raw-capture --ethertype frames", [&](auto v) { c.listen = v; });
    specs.add<int>("listen-secs", "Receiver duration (s)", c.listen_secs, [&](auto v) { c.listen_secs = v; });
    specs.add<std::string>("from", "Count only frames from this src MAC (receiver)", "", [&](auto v) {
        if (!v.empty()) {
            if (!parse_mac(v, c.from)) {
                statusbar::throw_or_abort(std::errc::invalid_argument, "invalid --from MAC");
            }
            c.from_set = true;
        }
    });
    return specs;
}

void print_usage(char const* prog, statusbar::args::ArgumentSpecs const& specs)
{
    std::print(stderr, "Usage: {} --interface=IFACE [options]\n\nOptions:\n", prog);
    std::string help;
    specs.format_help_to(std::back_inserter(help));
    std::print(stderr, "{}", help);
    std::print(
        stderr,
        "\nReceiver example (run on the OTHER host on the same switch):\n  {} --interface=eth0 --listen --from=<sender-mac>\n",
        prog);
}

// Read one counter from /sys/class/net/<iface>/statistics/<name>; -1 on error.
auto read_iface_stat(std::string const& iface, char const* name) -> long long
{
    std::string const path = "/sys/class/net/" + iface + "/statistics/" + name;
    std::ifstream f(path);
    long long v = -1;
    if (f) {
        f >> v;
    }
    return v;
}

struct TxStats
{
    long long packets = -1;
    long long bytes = -1;
    long long dropped = -1;
    long long errors = -1;
    long long carrier = -1;
    long long fifo = -1;
};

auto sample_tx(std::string const& iface) -> TxStats
{
    return TxStats{
        .packets = read_iface_stat(iface, "tx_packets"),
        .bytes = read_iface_stat(iface, "tx_bytes"),
        .dropped = read_iface_stat(iface, "tx_dropped"),
        .errors = read_iface_stat(iface, "tx_errors"),
        .carrier = read_iface_stat(iface, "tx_carrier_errors"),
        .fifo = read_iface_stat(iface, "tx_fifo_errors"),
    };
}

void print_tx_delta(TxStats const& before, TxStats const& after)
{
    auto d = [](long long a, long long b) { return (a < 0 || b < 0) ? -1 : (b - a); };
    std::printf(
        "  driver /sys counters delta: tx_packets=%+lld tx_bytes=%+lld tx_dropped=%+lld "
        "tx_errors=%+lld tx_carrier_errors=%+lld tx_fifo_errors=%+lld\n",
        d(before.packets, after.packets),
        d(before.bytes, after.bytes),
        d(before.dropped, after.dropped),
        d(before.errors, after.errors),
        d(before.carrier, after.carrier),
        d(before.fifo, after.fifo));
}

// Build an ENTITY_AVAILABLE ADPDU (68 bytes) with a fixed test entity id.
auto build_adpdu(std::array<uint8_t, 6> const& src_mac) -> std::array<uint8_t, ADPDU_LEN>
{
    std::array<uint8_t, ADPDU_LEN> p{};
    p[0] = 0xFA;  // AVTP subtype = ADP
    p[1] = 0x00;  // sv=0, version=0, message_type=0 (ENTITY_AVAILABLE)
    // valid_time (5 bits) << 3 | control_data_length high bits; control_data_length = 56
    p[2] = static_cast<uint8_t>((31u << 3) | ((56u >> 8) & 0x07));  // valid_time=31
    p[3] = static_cast<uint8_t>(56u & 0xFF);                        // control_data_length low
    // entity_id (8): MAC-derived EUI64 (mac[0..2] ff fe mac[3..5]) + unique 0x0002
    p[4] = src_mac[0];
    p[5] = src_mac[1];
    p[6] = src_mac[2];
    p[7] = 0xFF;
    p[8] = 0xFE;
    p[9] = src_mac[3];
    p[10] = src_mac[4];
    p[11] = src_mac[5];
    // entity_model_id (8) at [12..19] left zero
    // entity_capabilities (4) at [20..23]: AEM(0x01)|CLASS_A(0x08)|GPTP(0x... ) — use 0x00000508
    p[20] = 0x00;
    p[21] = 0x00;
    p[22] = 0x05;
    p[23] = 0x08;
    // talker_stream_sources [24..25]=0, talker_caps [26..27], listener_sinks [28..29]=1
    p[29] = 0x01;
    // listener_caps [30..31], controller_caps [32..35], available_index [36..39]
    // gptp_grandmaster_id [40..47], domain [48], reserved... association_id [56..63], reserved [64..67]
    return p;
}

void hexdump(std::span<uint8_t const> b)
{
    for (size_t i = 0; i < b.size(); ++i) {
        std::printf("%02x%s", b[i], ((i + 1) % 16 == 0) ? "\n" : " ");
    }
    if (b.size() % 16 != 0) {
        std::printf("\n");
    }
}

// One probe run with a specific bypass setting. Returns 0 on success.
auto run_probe(Config const& cfg, int ifindex, std::array<uint8_t, 6> const& mac, bool bypass) -> int
{
    std::printf("\n=== run: PACKET_QDISC_BYPASS=%d bind=%d ===\n", bypass ? 1 : 0, cfg.bind_sock);

    int const fd = ::socket(AF_PACKET, SOCK_RAW, htons(cfg.ethertype));
    if (fd < 0) {
        std::printf("  socket(AF_PACKET, SOCK_RAW) FAILED errno=%d (%s) — need CAP_NET_RAW?\n", errno, std::strerror(errno));
        return 1;
    }

    // setsockopt(PACKET_QDISC_BYPASS) — report success and read it back.
    if (bypass) {
        int const on = 1;
        if (::setsockopt(fd, SOL_PACKET, PACKET_QDISC_BYPASS, &on, sizeof(on)) < 0) {
            std::printf("  setsockopt(PACKET_QDISC_BYPASS=1) FAILED errno=%d (%s)\n", errno, std::strerror(errno));
        } else {
            int readback = -1;
            socklen_t len = sizeof(readback);
            if (::getsockopt(fd, SOL_PACKET, PACKET_QDISC_BYPASS, &readback, &len) == 0) {
                std::printf("  setsockopt(PACKET_QDISC_BYPASS=1) OK; readback=%d\n", readback);
            } else {
                std::printf("  setsockopt(PACKET_QDISC_BYPASS=1) OK; readback unavailable (errno=%d)\n", errno);
            }
        }
    } else {
        std::printf("  PACKET_QDISC_BYPASS not set (normal qdisc path)\n");
    }

    struct sockaddr_ll addr{};
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(cfg.ethertype);
    addr.sll_ifindex = ifindex;
    addr.sll_halen = ETH_ALEN;
    std::memcpy(addr.sll_addr, cfg.dest.data(), ETH_ALEN);

    if (cfg.bind_sock != 0) {
        if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::printf("  bind() FAILED errno=%d (%s)\n", errno, std::strerror(errno));
        } else {
            std::printf("  bind() OK\n");
        }
    }

    // Build the frame: 14-byte Ethernet header + ADPDU payload.
    auto const adpdu = build_adpdu(mac);
    auto const payload_len = static_cast<size_t>(cfg.payload_len) <= ADPDU_LEN ? static_cast<size_t>(cfg.payload_len) : ADPDU_LEN;
    std::vector<uint8_t> frame;
    frame.reserve(ETH_HDR + MIN_ETH_PAYLOAD);
    frame.insert(frame.end(), cfg.dest.begin(), cfg.dest.end());
    frame.insert(frame.end(), mac.begin(), mac.end());
    frame.push_back(static_cast<uint8_t>(cfg.ethertype >> 8));
    frame.push_back(static_cast<uint8_t>(cfg.ethertype & 0xFF));
    frame.insert(frame.end(), adpdu.begin(), adpdu.begin() + static_cast<long>(payload_len));

    bool const is_runt = frame.size() < MIN_ETH_PAYLOAD;
    if (cfg.pad != 0 && is_runt) {
        frame.resize(MIN_ETH_PAYLOAD, 0);
        std::printf("  padded runt frame up to %zu bytes\n", frame.size());
    }
    std::printf(
        "  frame length = %zu bytes (eth_hdr=%zu + payload=%zu) %s\n",
        frame.size(),
        ETH_HDR,
        payload_len,
        frame.size() < MIN_ETH_PAYLOAD ? "[RUNT < 60 — driver may drop]" : "[ok, >= 60]");
    if (cfg.verbose) {
        hexdump(frame);
    }

    TxStats const before = sample_tx(cfg.interface);

    int ok = 0;
    int fail = 0;
    for (int i = 0; i < cfg.count; ++i) {
        ssize_t const n = ::sendto(fd, frame.data(), frame.size(), 0, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        if (n < 0) {
            ++fail;
            std::printf("  [%d] sendto FAILED errno=%d (%s)\n", i, errno, std::strerror(errno));
        } else {
            ++ok;
            std::printf("  [%d] sendto OK (%zd bytes)\n", i, n);
        }
        if (i + 1 < cfg.count && cfg.interval_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{cfg.interval_ms});
        }
    }

    // Give the driver a moment to update counters.
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    TxStats const after = sample_tx(cfg.interface);

    std::printf("  sendto: %d ok, %d failed (of %d)\n", ok, fail, cfg.count);
    print_tx_delta(before, after);
    long long const dpkts = (before.packets < 0 || after.packets < 0) ? -1 : (after.packets - before.packets);
    if (dpkts >= 0 && ok > 0) {
        if (dpkts >= ok) {
            std::printf(
                "  => driver counted >= our sends as TX'd. If tcpdump saw nothing, loss is past the counter "
                "(offload/HW/switch).\n");
        } else {
            std::printf(
                "  => driver TX'd only %lld of %d accepted frames. The driver is DROPPING after sendto succeeded.\n", dpkts, ok);
        }
    }

    ::close(fd);
    return 0;
}

// Receiver: raw-capture frames of cfg.ethertype on the iface for listen_secs,
// counting how many came from cfg.from (if set). Run on a host on the same
// switch as the sender to confirm whether the sender's frames hit the wire.
auto run_listen(Config const& cfg, int ifindex) -> int
{
    int const fd = ::socket(AF_PACKET, SOCK_RAW, htons(cfg.ethertype));
    if (fd < 0) {
        std::printf("  socket() FAILED errno=%d (%s) — need CAP_NET_RAW?\n", errno, std::strerror(errno));
        return 1;
    }

    struct sockaddr_ll addr{};
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(cfg.ethertype);
    addr.sll_ifindex = ifindex;
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::printf("  bind() FAILED errno=%d (%s)\n", errno, std::strerror(errno));
        ::close(fd);
        return 1;
    }

    // Go promiscuous so the NIC delivers multicast/other frames (ADP is sent to
    // a multicast MAC; without this the hardware filter drops it before us).
    struct packet_mreq mreq{};
    mreq.mr_ifindex = ifindex;
    mreq.mr_type = PACKET_MR_PROMISC;
    if (::setsockopt(fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        std::printf("  PACKET_MR_PROMISC FAILED errno=%d (%s) — may miss multicast\n", errno, std::strerror(errno));
    }

    timeval tv{.tv_sec = 1, .tv_usec = 0};
    (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::printf("\n=== LISTEN on %s for %ds (ethertype 0x%04x)", cfg.interface.c_str(), cfg.listen_secs, cfg.ethertype);
    if (cfg.from_set) {
        std::printf(
            ", counting frames from %02x:%02x:%02x:%02x:%02x:%02x",
            cfg.from[0],
            cfg.from[1],
            cfg.from[2],
            cfg.from[3],
            cfg.from[4],
            cfg.from[5]);
    }
    std::printf(" ===\n");

    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{cfg.listen_secs};
    std::array<uint8_t, 2048> buf{};
    long total = 0;
    long from_match = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        ssize_t const n = ::recv(fd, buf.data(), buf.size(), 0);
        if (n < 14) {
            continue;  // timeout or runt
        }
        ++total;
        bool const match = cfg.from_set && std::memcmp(&buf[6], cfg.from.data(), 6) == 0;
        if (match) {
            ++from_match;
        }
        if (cfg.verbose || match) {
            std::printf(
                "  rx src=%02x:%02x:%02x:%02x:%02x:%02x len=%zd%s\n",
                buf[6],
                buf[7],
                buf[8],
                buf[9],
                buf[10],
                buf[11],
                n,
                match ? "  <== from target" : "");
        }
    }

    std::printf("  received %ld frames of ethertype 0x%04x", total, cfg.ethertype);
    if (cfg.from_set) {
        std::printf(
            "; %ld from target MAC %s\n",
            from_match,
            from_match > 0 ? "(sender's frames REACH the wire)" : "(none — sender's frames did NOT reach the wire)");
    } else {
        std::printf("\n");
    }
    ::close(fd);
    return 0;
}

}  // namespace

auto main(int argc, char** argv) -> int
{
    Config cfg;
    auto specs = build_arg_specs(cfg);
    auto const cli_result = statusbar::config::parse_cli_args(argc, argv, specs, print_usage, "statusbar-adp-l2send-probe");
    if (!cli_result) {
        return statusbar::config::handled_builtin_command(cli_result) ? 0 : 1;
    }

    if (cfg.interface.empty()) {
        std::print(stderr, "error: --interface is required\n\n");
        print_usage(argv[0], specs);
        return 2;
    }

    // Resolve ifindex, MAC, MTU, link flags via a throwaway socket.
    int const probe_fd = ::socket(AF_PACKET, SOCK_RAW, htons(ETHERTYPE_AVTP));
    if (probe_fd < 0) {
        std::printf("socket() FAILED errno=%d (%s) — need CAP_NET_RAW (root or setcap)?\n", errno, std::strerror(errno));
        return 1;
    }

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, cfg.interface.c_str(), IFNAMSIZ - 1);

    if (::ioctl(probe_fd, SIOCGIFINDEX, &ifr) < 0) {
        std::printf("SIOCGIFINDEX(%s) FAILED errno=%d (%s)\n", cfg.interface.c_str(), errno, std::strerror(errno));
        ::close(probe_fd);
        return 1;
    }
    int const ifindex = ifr.ifr_ifindex;

    std::array<uint8_t, 6> mac{};
    if (::ioctl(probe_fd, SIOCGIFHWADDR, &ifr) < 0) {
        std::printf("SIOCGIFHWADDR FAILED errno=%d (%s)\n", errno, std::strerror(errno));
        ::close(probe_fd);
        return 1;
    }
    for (int i = 0; i < 6; ++i) {
        mac[static_cast<size_t>(i)] = static_cast<uint8_t>(ifr.ifr_hwaddr.sa_data[i]);
    }

    int mtu = -1;
    if (::ioctl(probe_fd, SIOCGIFMTU, &ifr) == 0) {
        mtu = ifr.ifr_mtu;
    }
    short flags = 0;
    if (::ioctl(probe_fd, SIOCGIFFLAGS, &ifr) == 0) {
        flags = ifr.ifr_flags;
    }
    ::close(probe_fd);

    std::printf("interface: %s  ifindex=%d  mtu=%d\n", cfg.interface.c_str(), ifindex, mtu);
    std::printf("src MAC:   %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    std::printf(
        "dest MAC:  %02x:%02x:%02x:%02x:%02x:%02x  ethertype=0x%04x\n",
        cfg.dest[0],
        cfg.dest[1],
        cfg.dest[2],
        cfg.dest[3],
        cfg.dest[4],
        cfg.dest[5],
        cfg.ethertype);
    std::printf("link flags: UP=%d RUNNING=%d\n", (flags & IFF_UP) ? 1 : 0, (flags & IFF_RUNNING) ? 1 : 0);
    std::printf(
        "(run `tcpdump -i %s -e -nn ether proto 0x%04x` in another shell to watch the wire)\n",
        cfg.interface.c_str(),
        cfg.ethertype);

    if (cfg.listen) {
        return run_listen(cfg, ifindex);
    }

    if (cfg.compare) {
        int rc = run_probe(cfg, ifindex, mac, false);
        rc |= run_probe(cfg, ifindex, mac, true);
        std::printf("\nCompare tx_packets deltas above: a smaller delta under BYPASS=1 confirms QDISC_BYPASS drops frames.\n");
        return rc;
    }
    return run_probe(cfg, ifindex, mac, cfg.bypass != 0);
}
