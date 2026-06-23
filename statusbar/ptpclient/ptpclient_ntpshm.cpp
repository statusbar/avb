// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// @file ptpclient_ntpshm.cpp
/// @brief Implementation of the NTP SHM-backed PtpClientBase driver.

#include "statusbar/ptpclient/ptpclient_ntpshm.hpp"

#if defined(__linux__)

#    include <charconv>
#    include <string>
#    include <system_error>
#    include <utility>

namespace statusbar::ptpclient {

auto NtpShmPtpClient::open(std::string_view device_path) noexcept -> statusbar::Status
{
    int segment = 0;
    if (!device_path.empty()) {
        // Parse the segment index from the device_path string (e.g. "0", "1").
        // Uses std::from_chars for locale-independent, allocation-free integer parsing.
        auto result = std::from_chars(device_path.data(), device_path.data() + device_path.size(), segment);
        if (result.ec != std::errc{}) {
            return statusbar::failure(PtpError::device_not_open);
        }
    }

    auto reader_result = gptp::NtpShmReader::open(segment);
    if (!reader_result) {
        return statusbar::failure(PtpError::device_not_open);
    }

    reader_ = std::make_unique<gptp::NtpShmReader>(std::move(*reader_result));
    device_name_ = std::string("ntpshm:") + std::string(device_path.empty() ? "0" : device_path);
    return statusbar::success();
}

}  // namespace statusbar::ptpclient

#endif  // __linux__
