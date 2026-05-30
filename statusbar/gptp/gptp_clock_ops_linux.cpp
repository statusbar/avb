// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/gptp/gptp_clock_ops_linux.hpp"

#if defined(__linux__)

#    include "statusbar/buffer/span_utils.hpp"
#    include "statusbar/gptp/gptp_base.hpp"
#    include "statusbar/gptp/gptp_linux_socket.hpp"
#    include "statusbar/ieee/ieee.hpp"
#    include "statusbar/net/net_posix_util.hpp"
#    include "statusbar/net/net_rawnet.hpp"

#    include <poll.h>
#    include <unistd.h>

#    include <cerrno>
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
#    include <sys/timex.h>

namespace statusbar::gptp {

namespace {

/// Convert an open PHC fd to a clockid_t that clock_gettime / clock_adjtime accept.
/// Equivalent to the kernel's FD_TO_CLOCKID macro:
///   #define FD_TO_CLOCKID(fd) ((~(clockid_t)(fd) << 3) | 3)
auto fd_to_clockid(int fd) -> clockid_t
{
    return ((~static_cast<clockid_t>(fd)) << 3) | 3;
}

}  // namespace

auto make_linux_phc_clock_ops(int raw_sock_fd, int phc_fd, std::string ifname, int if_index, ieee::Eui48 src_mac) -> GptpClockOps
{
    clockid_t const phc_clock_id = fd_to_clockid(phc_fd);

    GptpClockOps ops{};

    // ---------------------------------------------------------------
    // 1. get_local_time_ns — read the NIC's PTP hardware clock
    // ---------------------------------------------------------------
    ops.get_local_time_ns = [phc_clock_id]() -> int64_t {
        struct timespec ts{};
        clock_gettime(phc_clock_id, &ts);
        return (ts.tv_sec * 1'000'000'000LL) + ts.tv_nsec;
    };

    // ---------------------------------------------------------------
    // 2. send_frame — transmit gPTP payload, harvest HW TX timestamp
    // ---------------------------------------------------------------
    ops.send_frame = [raw_sock_fd, if_index, src_mac](std::span<uint8_t const> gptp_payload) -> TxResult {
        // Build full Ethernet frame: dst + src + ethertype + payload
        std::array<uint8_t, net::MAX_ETHERNET_FRAME_SIZE> frame{};
        size_t const payload_len = gptp_payload.size();
        size_t const frame_len = ieee::protocols::ETHERNET_HEADER_SIZE + payload_len;
        if (frame_len > frame.size()) {
            return TxResult{.ok = false, .tx_timestamp_ns = 0};
        }

        // Destination: gPTP multicast
        span_copy(make_span(frame, {.length = 6}), make_const_span(GPTP_MULTICAST_MAC.value));
        // Source: our MAC
        span_copy(make_span(frame, {.start = 6, .length = 6}), make_const_span(src_mac.value));
        // EtherType: 0x88F7
        frame[12] = static_cast<uint8_t>(GPTP_ETHERTYPE >> 8);
        frame[13] = static_cast<uint8_t>(GPTP_ETHERTYPE & 0xFF);
        // Payload
        span_copy(make_span(frame, {.start = ieee::protocols::ETHERNET_HEADER_SIZE}), gptp_payload);

        // Send via sockaddr_ll
        struct sockaddr_ll dst_addr{};
        dst_addr.sll_family = AF_PACKET;
        dst_addr.sll_protocol = htons(GPTP_ETHERTYPE);
        dst_addr.sll_ifindex = if_index;
        dst_addr.sll_halen = ETH_ALEN;
        std::memcpy(dst_addr.sll_addr, GPTP_MULTICAST_MAC.value.data(), 6);

        ssize_t sent = 0;
        do {
            sent = ::sendto(raw_sock_fd, frame.data(), frame_len, 0, net::sockaddr_cast(dst_addr), sizeof(dst_addr));
        } while (sent < 0 && errno == EINTR);

        if (sent < 0) {
            return TxResult{.ok = false, .tx_timestamp_ns = 0};
        }

        // Harvest HW TX timestamp from the socket error queue.
        // The kernel delivers it asynchronously via MSG_ERRQUEUE.
        // Poll briefly, then retry up to 10 times (100 µs each).
        struct pollfd pfd{};
        pfd.fd = raw_sock_fd;
        pfd.events = POLLERR;

        for (int retry = 0; retry < 10; ++retry) {
            int const ready = ::poll(&pfd, 1, 1);  // 1 ms timeout
            if (ready <= 0) {
                continue;
            }

            struct msghdr msg{};
            struct iovec iov{};
            uint8_t ctrl_buf[256]{};
            uint8_t dummy_buf[256]{};

            iov.iov_base = dummy_buf;
            iov.iov_len = sizeof(dummy_buf);
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            msg.msg_control = ctrl_buf;
            msg.msg_controllen = sizeof(ctrl_buf);

            ssize_t const n = ::recvmsg(raw_sock_fd, &msg, MSG_ERRQUEUE);
            if (n < 0) {
                continue;
            }

            int64_t const hw_ns = extract_so_timestamping_ns(msg, /*prefer_hw=*/true);
            if (hw_ns != 0) {
                return TxResult{.ok = true, .tx_timestamp_ns = hw_ns};
            }
        }

        // Frame was sent but we couldn't get a HW timestamp
        return TxResult{.ok = false, .tx_timestamp_ns = 0};
    };

    // ---------------------------------------------------------------
    // 3. adjust_phase_ns — step the PHC clock
    //
    // Most PHC drivers will accept ADJ_SETOFFSET for "small" deltas
    // (typically up to a second or two), but reject large deltas
    // silently — `clock_adjtime` returns -1/EINVAL/EOVERFLOW. For
    // huge first-sync corrections (e.g. when GM time and PHC time
    // are in different epochs) we therefore fall back to
    // `clock_settime` with the absolute target time.
    //
    // Two-step normalization: when phase_ns is negative, naive
    // tv_sec / tv_usec computation produces a negative tv_usec which
    // some PHC drivers reject. Force tv_usec into [0, 1e9) by
    // borrowing from tv_sec.
    // ---------------------------------------------------------------
    ops.adjust_phase_ns = [phc_clock_id](int64_t phase_ns) {
        struct timex tmx{};
        tmx.modes = ADJ_SETOFFSET | ADJ_NANO;
        int64_t sec = phase_ns / 1'000'000'000LL;
        int64_t nsec = phase_ns % 1'000'000'000LL;
        if (nsec < 0) {
            nsec += 1'000'000'000LL;
            sec -= 1;
        }
        tmx.time.tv_sec = static_cast<__time_t>(sec);
        // tv_usec carries nanoseconds when ADJ_NANO is set.
        tmx.time.tv_usec = static_cast<__suseconds_t>(nsec);
        if (clock_adjtime(phc_clock_id, &tmx) == 0) {
            return;
        }
        int const adj_errno = errno;
        // Fall back to clock_settime with the absolute target time
        // for offsets the driver wouldn't accept as a delta.
        struct timespec cur{};
        if (clock_gettime(phc_clock_id, &cur) != 0) {
            std::println(
                stderr,
                "[gptp] adjust_phase_ns({} ns) failed: clock_adjtime errno={} ({}); clock_gettime also failed",
                static_cast<long long>(phase_ns),
                adj_errno,
                std::strerror(adj_errno));
            return;
        }
        int64_t const target_ns = (cur.tv_sec * 1'000'000'000LL) + cur.tv_nsec + phase_ns;
        struct timespec target{};
        target.tv_sec = static_cast<__time_t>(target_ns / 1'000'000'000LL);
        target.tv_nsec = static_cast<long>(target_ns % 1'000'000'000LL);
        if (target.tv_nsec < 0) {
            target.tv_nsec += 1'000'000'000L;
            target.tv_sec -= 1;
        }
        if (clock_settime(phc_clock_id, &target) != 0) {
            std::println(
                stderr,
                "[gptp] adjust_phase_ns({} ns) fallback clock_settime failed: errno={} ({})",
                static_cast<long long>(phase_ns),
                errno,
                std::strerror(errno));
        }
    };

    // ---------------------------------------------------------------
    // 4. adjust_frequency_ppb — slew the PHC oscillator
    // ---------------------------------------------------------------
    ops.adjust_frequency_ppb = [phc_clock_id](double ppb) {
        struct timex tmx{};
        tmx.modes = ADJ_FREQUENCY;
        // kernel freq units: ppb × 65.536 (i.e. ppb shifted into
        // the 16-bit fractional fixed-point the kernel expects)
        tmx.freq = static_cast<long>(ppb * 65.536);
        if (clock_adjtime(phc_clock_id, &tmx) != 0) {
            std::println(stderr, "[gptp] adjust_frequency_ppb({:.3f}) failed: errno={} ({})", ppb, errno, std::strerror(errno));
        }
    };

    // ---------------------------------------------------------------
    // 5. get_link_speed — query ethtool for NIC speed
    // ---------------------------------------------------------------
    ops.get_link_speed = [raw_sock_fd, ifname = std::move(ifname)]() -> LinkSpeedMbps {
        struct ifreq ifr{};
        struct ethtool_cmd ecmd{};
        std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
        ecmd.cmd = ETHTOOL_GSET;
        net::set_ifr_data(ifr, &ecmd);
        if (::ioctl(raw_sock_fd, SIOCETHTOOL, &ifr) < 0) {
            return LinkSpeedMbps::Unknown;
        }
        switch (ethtool_cmd_speed(&ecmd)) {
            case 10:
                return LinkSpeedMbps::Mbps10;
            case 100:
                return LinkSpeedMbps::Mbps100;
            case 1000:
                return LinkSpeedMbps::Mbps1000;
            case 10000:
                return LinkSpeedMbps::Mbps10000;
            default:
                return LinkSpeedMbps::Unknown;
        }
    };

    return ops;
}

// =================================================================
// Hardware-timestamping factory in passthrough mode (no PHC discipline)
// =================================================================
//
// Same hardware timestamping path as make_linux_phc_clock_ops, but the
// PHC is never written to. Phase / frequency corrections accumulate in
// the supplied SoftClock; reads return raw_PHC + virtual offset; wire
// timestamps are corrected before being returned to the protocol stack
// (the slave session still must call soft_clock.correct_timestamp on
// RX timestamps before feeding them to receive_frame, mirroring the
// SW factory's contract).

auto make_linux_phc_passthrough_ops(
    int raw_sock_fd, int phc_fd, SoftClock& soft_clock, std::string ifname, int if_index, ieee::Eui48 src_mac) -> GptpClockOps
{
    clockid_t const phc_clock_id = fd_to_clockid(phc_fd);
    GptpClockOps ops{};

    // 1. get_local_time_ns — raw PHC + SoftClock virtual offset.
    ops.get_local_time_ns = [phc_clock_id, &soft_clock]() -> int64_t {
        struct timespec ts{};
        clock_gettime(phc_clock_id, &ts);
        int64_t const raw = (ts.tv_sec * 1'000'000'000LL) + ts.tv_nsec;
        return soft_clock.correct_timestamp(raw);
    };

    // 2. send_frame — same as the disciplined HW path, but the
    // returned TX timestamp is corrected via the SoftClock so it lands
    // in the same domain the servo's offset math expects.
    ops.send_frame = [raw_sock_fd, if_index, src_mac, &soft_clock](std::span<uint8_t const> gptp_payload) -> TxResult {
        std::array<uint8_t, net::MAX_ETHERNET_FRAME_SIZE> frame{};
        size_t const payload_len = gptp_payload.size();
        size_t const frame_len = ieee::protocols::ETHERNET_HEADER_SIZE + payload_len;
        if (frame_len > frame.size()) {
            return TxResult{.ok = false, .tx_timestamp_ns = 0};
        }
        span_copy(make_span(frame, {.length = 6}), make_const_span(GPTP_MULTICAST_MAC.value));
        span_copy(make_span(frame, {.start = 6, .length = 6}), make_const_span(src_mac.value));
        frame[12] = static_cast<uint8_t>(GPTP_ETHERTYPE >> 8);
        frame[13] = static_cast<uint8_t>(GPTP_ETHERTYPE & 0xFF);
        span_copy(make_span(frame, {.start = ieee::protocols::ETHERNET_HEADER_SIZE}), gptp_payload);

        struct sockaddr_ll dst_addr{};
        dst_addr.sll_family = AF_PACKET;
        dst_addr.sll_protocol = htons(GPTP_ETHERTYPE);
        dst_addr.sll_ifindex = if_index;
        dst_addr.sll_halen = ETH_ALEN;
        std::memcpy(dst_addr.sll_addr, GPTP_MULTICAST_MAC.value.data(), 6);

        ssize_t sent = 0;
        do {
            sent = ::sendto(raw_sock_fd, frame.data(), frame_len, 0, net::sockaddr_cast(dst_addr), sizeof(dst_addr));
        } while (sent < 0 && errno == EINTR);
        if (sent < 0) {
            return TxResult{.ok = false, .tx_timestamp_ns = 0};
        }

        struct pollfd pfd{};
        pfd.fd = raw_sock_fd;
        pfd.events = POLLERR;
        for (int retry = 0; retry < 10; ++retry) {
            int const ready = ::poll(&pfd, 1, 1);
            if (ready <= 0) {
                continue;
            }
            struct msghdr msg{};
            struct iovec iov{};
            uint8_t ctrl_buf[256]{};
            uint8_t dummy_buf[256]{};
            iov.iov_base = dummy_buf;
            iov.iov_len = sizeof(dummy_buf);
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            msg.msg_control = ctrl_buf;
            msg.msg_controllen = sizeof(ctrl_buf);
            ssize_t const n = ::recvmsg(raw_sock_fd, &msg, MSG_ERRQUEUE);
            if (n < 0) {
                continue;
            }
            int64_t const hw_ns = extract_so_timestamping_ns(msg, /*prefer_hw=*/true);
            if (hw_ns != 0) {
                return TxResult{.ok = true, .tx_timestamp_ns = soft_clock.correct_timestamp(hw_ns)};
            }
        }
        return TxResult{.ok = false, .tx_timestamp_ns = 0};
    };

    // 3. adjust_phase_ns — virtual offset, no PHC modification.
    ops.adjust_phase_ns = [phc_clock_id, &soft_clock](int64_t phase_ns) {
        struct timespec ts{};
        clock_gettime(phc_clock_id, &ts);
        int64_t const raw_now = (ts.tv_sec * 1'000'000'000LL) + ts.tv_nsec;
        soft_clock.adjust_phase(phase_ns, raw_now);
    };

    // 4. adjust_frequency_ppb — virtual rate, no clock_adjtime.
    ops.adjust_frequency_ppb = [phc_clock_id, &soft_clock](double ppb) {
        struct timespec ts{};
        clock_gettime(phc_clock_id, &ts);
        int64_t const raw_now = (ts.tv_sec * 1'000'000'000LL) + ts.tv_nsec;
        soft_clock.adjust_frequency(ppb, raw_now);
    };

    // 5. get_link_speed — same ethtool query.
    ops.get_link_speed = [raw_sock_fd, ifname = std::move(ifname)]() -> LinkSpeedMbps {
        struct ifreq ifr{};
        struct ethtool_cmd ecmd{};
        std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
        ecmd.cmd = ETHTOOL_GSET;
        net::set_ifr_data(ifr, &ecmd);
        if (::ioctl(raw_sock_fd, SIOCETHTOOL, &ifr) < 0) {
            return LinkSpeedMbps::Unknown;
        }
        switch (ethtool_cmd_speed(&ecmd)) {
            case 10:
                return LinkSpeedMbps::Mbps10;
            case 100:
                return LinkSpeedMbps::Mbps100;
            case 1000:
                return LinkSpeedMbps::Mbps1000;
            case 10000:
                return LinkSpeedMbps::Mbps10000;
            default:
                return LinkSpeedMbps::Unknown;
        }
    };

    return ops;
}

// =================================================================
// Software-only timestamping factory (no PHC)
// =================================================================

auto make_linux_sw_clock_ops(int raw_sock_fd, SoftClock& soft_clock, std::string ifname, int if_index, ieee::Eui48 src_mac)
    -> GptpClockOps
{
    GptpClockOps ops{};

    // ---------------------------------------------------------------
    // 1. get_local_time_ns — read CLOCK_REALTIME with virtual corrections
    // ---------------------------------------------------------------
    ops.get_local_time_ns = [&soft_clock]() -> int64_t { return soft_clock.get_ptp_time_ns(clock_realtime_ns()); };

    // ---------------------------------------------------------------
    // 2. send_frame — transmit gPTP payload, harvest SOFTWARE TX timestamp
    // ---------------------------------------------------------------
    ops.send_frame = [raw_sock_fd, if_index, src_mac, &soft_clock](std::span<uint8_t const> gptp_payload) -> TxResult {
        // Build full Ethernet frame: dst + src + ethertype + payload
        std::array<uint8_t, net::MAX_ETHERNET_FRAME_SIZE> frame{};
        size_t const payload_len = gptp_payload.size();
        size_t const frame_len = ieee::protocols::ETHERNET_HEADER_SIZE + payload_len;
        if (frame_len > frame.size()) {
            return TxResult{.ok = false, .tx_timestamp_ns = 0};
        }

        span_copy(make_span(frame, {.length = 6}), make_const_span(GPTP_MULTICAST_MAC.value));
        span_copy(make_span(frame, {.start = 6, .length = 6}), make_const_span(src_mac.value));
        frame[12] = static_cast<uint8_t>(GPTP_ETHERTYPE >> 8);
        frame[13] = static_cast<uint8_t>(GPTP_ETHERTYPE & 0xFF);
        span_copy(make_span(frame, {.start = ieee::protocols::ETHERNET_HEADER_SIZE}), gptp_payload);

        struct sockaddr_ll dst_addr{};
        dst_addr.sll_family = AF_PACKET;
        dst_addr.sll_protocol = htons(GPTP_ETHERTYPE);
        dst_addr.sll_ifindex = if_index;
        dst_addr.sll_halen = ETH_ALEN;
        std::memcpy(dst_addr.sll_addr, GPTP_MULTICAST_MAC.value.data(), 6);

        ssize_t sent = 0;
        do {
            sent = ::sendto(raw_sock_fd, frame.data(), frame_len, 0, net::sockaddr_cast(dst_addr), sizeof(dst_addr));
        } while (sent < 0 && errno == EINTR);

        if (sent < 0) {
            return TxResult{.ok = false, .tx_timestamp_ns = 0};
        }

        // Harvest SOFTWARE TX timestamp from the socket error queue.
        struct pollfd pfd{};
        pfd.fd = raw_sock_fd;
        pfd.events = POLLERR;

        for (int retry = 0; retry < 10; ++retry) {
            int const ready = ::poll(&pfd, 1, 1);
            if (ready <= 0) {
                continue;
            }

            struct msghdr msg{};
            struct iovec iov{};
            uint8_t ctrl_buf[256]{};
            uint8_t dummy_buf[256]{};

            iov.iov_base = dummy_buf;
            iov.iov_len = sizeof(dummy_buf);
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            msg.msg_control = ctrl_buf;
            msg.msg_controllen = sizeof(ctrl_buf);

            ssize_t const n = ::recvmsg(raw_sock_fd, &msg, MSG_ERRQUEUE);
            if (n < 0) {
                continue;
            }

            int64_t const sw_ns = extract_so_timestamping_ns(msg, /*prefer_hw=*/false);
            if (sw_ns != 0) {
                // Apply virtual clock corrections to the raw SW timestamp
                // so the servo sees it in the corrected domain.
                return TxResult{.ok = true, .tx_timestamp_ns = soft_clock.correct_timestamp(sw_ns)};
            }
        }

        // Fallback: if we couldn't get a SW timestamp from the error queue,
        // use the current corrected time as an approximation.
        return TxResult{.ok = true, .tx_timestamp_ns = soft_clock.get_ptp_time_ns(clock_realtime_ns())};
    };

    // ---------------------------------------------------------------
    // 3. adjust_phase_ns — update the SoftClock virtual offset
    // ---------------------------------------------------------------
    ops.adjust_phase_ns = [&soft_clock](int64_t phase_ns) { soft_clock.adjust_phase(phase_ns, clock_realtime_ns()); };

    // ---------------------------------------------------------------
    // 4. adjust_frequency_ppb — update the SoftClock virtual rate
    // ---------------------------------------------------------------
    ops.adjust_frequency_ppb = [&soft_clock](double ppb) { soft_clock.adjust_frequency(ppb, clock_realtime_ns()); };

    // ---------------------------------------------------------------
    // 5. get_link_speed — same ethtool query as the PHC factory
    // ---------------------------------------------------------------
    ops.get_link_speed = [raw_sock_fd, ifname = std::move(ifname)]() -> LinkSpeedMbps {
        struct ifreq ifr{};
        struct ethtool_cmd ecmd{};
        std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
        ecmd.cmd = ETHTOOL_GSET;
        net::set_ifr_data(ifr, &ecmd);
        if (::ioctl(raw_sock_fd, SIOCETHTOOL, &ifr) < 0) {
            return LinkSpeedMbps::Unknown;
        }
        switch (ethtool_cmd_speed(&ecmd)) {
            case 10:
                return LinkSpeedMbps::Mbps10;
            case 100:
                return LinkSpeedMbps::Mbps100;
            case 1000:
                return LinkSpeedMbps::Mbps1000;
            case 10000:
                return LinkSpeedMbps::Mbps10000;
            default:
                return LinkSpeedMbps::Unknown;
        }
    };

    return ops;
}

}  // namespace statusbar::gptp

#endif  // __linux__
