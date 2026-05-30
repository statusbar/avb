// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/gptp/gptp_linux_socket.hpp"

#if defined(__linux__)

#    include "statusbar/buffer/span_utils.hpp"
#    include "statusbar/gptp/gptp_base.hpp"
#    include "statusbar/ieee/ieee.hpp"
#    include "statusbar/net/net_posix_util.hpp"
#    include "statusbar/net/net_rawnet.hpp"

#    include <fcntl.h>
#    include <unistd.h>

#    include <algorithm>
#    include <array>
#    include <cerrno>
#    include <chrono>
#    include <cstdio>
#    include <cstring>
#    include <ctime>
#    include <print>

#    include <arpa/inet.h>
#    include <linux/ethtool.h>
#    include <linux/if_ether.h>
#    include <linux/if_packet.h>
#    include <linux/net_tstamp.h>
#    include <linux/sockios.h>
#    include <net/if.h>
#    include <sys/ioctl.h>
#    include <sys/socket.h>

namespace statusbar::gptp {

auto get_interface_info(int sock_fd, char const* ifname) -> GptpInterfaceInfo
{
    GptpInterfaceInfo info{};

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    if (::ioctl(sock_fd, SIOCGIFINDEX, &ifr) < 0) {
        std::println(stderr, "SIOCGIFINDEX failed: {} ({})", std::strerror(errno), errno);
        return info;
    }
    info.if_index = ifr.ifr_ifindex;

    if (::ioctl(sock_fd, SIOCGIFHWADDR, &ifr) < 0) {
        std::println(stderr, "SIOCGIFHWADDR failed: {} ({})", std::strerror(errno), errno);
        info.if_index = -1;
        return info;
    }
    for (int i = 0; i < 6; ++i) {
        info.mac.value[i] = static_cast<uint8_t>(ifr.ifr_hwaddr.sa_data[i]);
    }

    return info;
}

auto open_raw_gptp_socket() -> int
{
    int const fd = ::socket(AF_PACKET, SOCK_RAW, htons(GPTP_ETHERTYPE));
    if (fd < 0) {
        std::println(stderr, "socket(AF_PACKET) failed: {} ({})", std::strerror(errno), errno);
        std::println(stderr, "Hint: grant CAP_NET_RAW with ./scripts/device/setcaps.sh");
    }
    return fd;
}

auto bind_gptp_socket(int sock_fd, int if_index) -> bool
{
    struct sockaddr_ll bind_addr{};
    bind_addr.sll_family = AF_PACKET;
    bind_addr.sll_protocol = htons(GPTP_ETHERTYPE);
    bind_addr.sll_ifindex = if_index;
    if (::bind(sock_fd, net::sockaddr_cast(bind_addr), sizeof(bind_addr)) < 0) {
        std::println(stderr, "bind failed: {} ({})", std::strerror(errno), errno);
        return false;
    }

    struct packet_mreq mr{};
    mr.mr_ifindex = if_index;
    mr.mr_type = PACKET_MR_MULTICAST;
    mr.mr_alen = 6;
    std::memcpy(mr.mr_address, GPTP_MULTICAST_MAC.value.data(), 6);
    if (::setsockopt(sock_fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) < 0) {
        std::println(stderr, "PACKET_ADD_MEMBERSHIP failed: {} ({})", std::strerror(errno), errno);
        return false;
    }

    return true;
}

auto enable_hw_timestamping(int sock_fd, char const* ifname) -> bool
{
    struct hwtstamp_config hwcfg{};
    hwcfg.tx_type = HWTSTAMP_TX_ON;
    hwcfg.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    net::set_ifr_data(ifr, &hwcfg);

    if (::ioctl(sock_fd, SIOCSHWTSTAMP, &ifr) < 0) {
        std::println(stderr, "SIOCSHWTSTAMP failed: {} ({})", std::strerror(errno), errno);
        return false;
    }
    return true;
}

auto enable_so_timestamping_hw(int sock_fd) -> bool
{
    int const flags = SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE;
    if (::setsockopt(sock_fd, SOL_SOCKET, SO_TIMESTAMPING, &flags, sizeof(flags)) < 0) {
        std::println(stderr, "SO_TIMESTAMPING (HW) failed: {} ({})", std::strerror(errno), errno);
        return false;
    }
    return true;
}

auto enable_so_timestamping_sw(int sock_fd) -> bool
{
    int const flags = SOF_TIMESTAMPING_TX_SOFTWARE | SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE;
    if (::setsockopt(sock_fd, SOL_SOCKET, SO_TIMESTAMPING, &flags, sizeof(flags)) < 0) {
        std::println(stderr, "SO_TIMESTAMPING (SW) failed: {} ({})", std::strerror(errno), errno);
        return false;
    }
    return true;
}

auto open_phc_for_interface(int sock_fd, char const* ifname) -> int
{
    struct ethtool_ts_info ts_info{};
    ts_info.cmd = ETHTOOL_GET_TS_INFO;

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    net::set_ifr_data(ifr, &ts_info);

    if (::ioctl(sock_fd, SIOCETHTOOL, &ifr) < 0) {
        std::println(stderr, "ETHTOOL_GET_TS_INFO failed: {} ({})", std::strerror(errno), errno);
        return -1;
    }

    int const phc_index = ts_info.phc_index;
    if (phc_index < 0) {
        std::println(stderr, "No PHC associated with {}", ifname);
        return -1;
    }

    char phc_path[32];
    std::snprintf(phc_path, sizeof(phc_path), "/dev/ptp%d", phc_index);

    int const phc_fd = ::open(phc_path, O_RDWR);
    if (phc_fd < 0) {
        std::println(stderr, "Failed to open {}: {} ({})", static_cast<char const*>(phc_path), std::strerror(errno), errno);
        return -1;
    }

    std::println(stderr, "PHC: {} (index {})", static_cast<char const*>(phc_path), phc_index);
    return phc_fd;
}

auto extract_so_timestamping_ns(struct msghdr const& msg, bool prefer_hw) -> int64_t
{
    for (auto* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(const_cast<struct msghdr*>(&msg), cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMPING) {
            auto const* ts = net::cmsg_data_as<struct timespec>(cmsg);
            // ts[0] = software (CLOCK_REALTIME), ts[1] = deprecated, ts[2] = raw hardware
            int const slot = prefer_hw ? 2 : 0;
            return (ts[slot].tv_sec * 1'000'000'000LL) + ts[slot].tv_nsec;
        }
    }
    return 0;
}

auto recv_gptp_frame(int sock_fd, std::span<uint8_t> payload_out, int64_t& rx_ns_out, bool prefer_hw) -> ssize_t
{
    std::array<uint8_t, net::MAX_ETHERNET_FRAME_SIZE> frame_buf{};
    uint8_t ctrl_buf[256]{};

    struct iovec iov{};
    iov.iov_base = frame_buf.data();
    iov.iov_len = frame_buf.size();

    struct msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = ctrl_buf;
    msg.msg_controllen = sizeof(ctrl_buf);

    ssize_t n = 0;
    do {
        n = ::recvmsg(sock_fd, &msg, 0);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        return -1;
    }

    auto const frame_len = static_cast<size_t>(n);
    if (frame_len < ieee::protocols::ETHERNET_HEADER_SIZE) {
        return -1;
    }

    rx_ns_out = extract_so_timestamping_ns(msg, prefer_hw);

    size_t const payload_len = frame_len - ieee::protocols::ETHERNET_HEADER_SIZE;
    auto const payload_in = make_const_span(frame_buf, {.start = ieee::protocols::ETHERNET_HEADER_SIZE, .length = payload_len});
    span_copy(payload_out, payload_in);
    size_t const copy_len = std::min(payload_len, payload_out.size());

    return static_cast<ssize_t>(copy_len);
}

auto clock_realtime_ns() -> int64_t
{
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return (ts.tv_sec * 1'000'000'000LL) + ts.tv_nsec;
}

auto compute_poll_timeout_ms(sm::TimePoint deadline, sm::TimePoint now, int cap_ms) -> int
{
    if (deadline == sm::TimePoint::max()) {
        return cap_ms;
    }
    auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    return std::max(0, std::min(static_cast<int>(remaining.count()), cap_ms));
}

}  // namespace statusbar::gptp

#endif  // __linux__
