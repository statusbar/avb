// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Implementation of PTP client factory function

#include "statusbar/ptpclient/ptpclient.hpp"

#include <memory>
#include <string_view>

namespace statusbar::ptpclient {

auto create_ptp_client(std::string_view driver_name, std::string_view device_path) -> StatusValue<std::unique_ptr<PtpClientBase>>
{
    // System clock fallback - works on all platforms
    if (driver_name == DRIVER_SYSTEM_CLOCK || driver_name == "system") {
        auto client = std::make_unique<SystemClockPtpClient>();
        if (auto status = client->open(device_path); !status) {
            return failure(status.error());
        }
        return success(std::unique_ptr<PtpClientBase>(std::move(client)));
    }

#if defined(__linux__)
    if (driver_name == DRIVER_LINUXPTP) {
        auto client = std::make_unique<LinuxPtpClient>();
        if (auto status = client->open(device_path); !status) {
            return failure(status.error());
        }
        return success(std::unique_ptr<PtpClientBase>(std::move(client)));
    }
#else
    if (driver_name == DRIVER_LINUXPTP) {
        return failure(PtpError::not_supported);
    }
#endif

    // Unknown driver
    return failure(PtpError::not_supported);
}

}  // namespace statusbar::ptpclient
