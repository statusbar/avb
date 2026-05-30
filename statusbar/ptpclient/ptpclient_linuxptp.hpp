#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Linux PTP Client implementation
/// Provides access to PTP hardware clocks via /dev/ptp* devices
/// Uses clock_gettime() with dynamic clock ID from the PTP device

#include <cstdint>
#include <cstring>

#if defined(__linux__)
#    include <fcntl.h>
#    include <unistd.h>

#    include <cerrno>
#    include <ctime>

#    include <linux/ptp_clock.h>
#    include <sys/ioctl.h>
#    include <sys/stat.h>
#    include <sys/timex.h>
#endif

#include "statusbar/ptpclient/ptpclient_base.hpp"
#include "statusbar/status/status.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <system_error>

namespace statusbar::ptpclient {

#if defined(__linux__)

/// Linux PTP client implementation
/// Opens a PTP device and reads hardware clock time using clock_gettime()
class LinuxPtpClient final : public PtpClientBase
{
  public:
    LinuxPtpClient() noexcept = default;

    /// Construct and open a PTP device
    /// @param device_path Path to the PTP device (default: "/dev/ptp0")
    /// @throws std::system_error if the device cannot be opened
    explicit LinuxPtpClient(std::string_view device_path);

    ~LinuxPtpClient() noexcept override;

    // Move operations
    LinuxPtpClient(LinuxPtpClient&& other) noexcept;
    auto operator=(LinuxPtpClient&& other) noexcept -> LinuxPtpClient&;

    [[nodiscard]] auto open(std::string_view device_path) noexcept -> Status override;
    void close() noexcept override;
    [[nodiscard]] auto is_open() const noexcept -> bool override { return fd_ >= 0; }
    [[nodiscard]] auto get_time_ns() const noexcept -> StatusValue<int64_t> override;
    [[nodiscard]] auto device_path() const noexcept -> std::string_view override { return device_path_; }

    /// Get the file descriptor (for advanced use)
    [[nodiscard]] auto fd() const noexcept -> int { return fd_; }

    /// Get the clock ID used with clock_gettime()
    [[nodiscard]] auto clock_id() const noexcept -> clockid_t { return clock_id_; }

    /// Get PTP clock capabilities (optional, for diagnostics)
    [[nodiscard]] auto get_capabilities() const noexcept -> StatusValue<ptp_clock_caps>;

  private:
    int fd_{-1};
    clockid_t clock_id_{-1};
    std::string device_path_;
};

#endif  // __linux__

}  // namespace statusbar::ptpclient
