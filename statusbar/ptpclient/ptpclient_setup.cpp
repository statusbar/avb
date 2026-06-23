// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/ptpclient/ptpclient_setup.hpp"

namespace statusbar::ptpclient {

auto create_client(PtpSetupConfig const& config) -> StatusValue<std::unique_ptr<PtpClientBase>>
{
    // System clock fallback - works on all platforms
    if (config.driver_name == "system" || config.driver_name == DRIVER_SYSTEM_CLOCK) {
        auto client = std::make_unique<SystemClockPtpClient>();
        if (auto status = client->open(config.device_path); !status) {
            return failure(status.error());
        }
        return success(std::unique_ptr<PtpClientBase>(std::move(client)));
    }

#if defined(__linux__)
    if (config.driver_name == "linuxptp") {
        auto client = std::make_unique<LinuxPtpClient>();
        if (auto status = client->open(config.device_path); !status) {
            return failure(status.error());
        }
        return success(std::unique_ptr<PtpClientBase>(std::move(client)));
    }
    if (config.driver_name == "ntpshm") {
        auto client = std::make_unique<NtpShmPtpClient>();
        if (auto status = client->open(config.device_path); !status) {
            return failure(status.error());
        }
        return success(std::unique_ptr<PtpClientBase>(std::move(client)));
    }
#else
    if (config.driver_name == "linuxptp") {
        return failure(PtpError::not_supported);
    }
    if (config.driver_name == "ntpshm") {
        return failure(PtpError::not_supported);
    }
#endif
    return failure(PtpError::not_supported);
}

auto setup_ptp(PtpSetupConfig const& config) -> StatusValue<PtpSetupResult>
{
    auto client_result = create_client(config);
    if (!client_result) {
        return forward_failure(client_result);
    }

    PtpSetupResult result;
    result.client = std::move(*client_result);
    result.bridge = std::make_unique<PtpTimeBridge>();

    return success(std::move(result));
}

auto load_ptp_app_config(config::Config const& toml_config) -> PtpAppConfig
{
    PtpAppConfig config;

    // Device configuration - platform-appropriate defaults
#if defined(__linux__)
    config.driver_name = std::string{toml_config.get_string("ptp.driver", "linuxptp")};
    config.device_path = std::string{toml_config.get_string("ptp.device", "/dev/ptp0")};
#else
    config.driver_name = std::string{toml_config.get_string("ptp.driver", "system")};
    config.device_path = std::string{toml_config.get_string("ptp.device", "system_clock")};
#endif

    // Timer configuration
    config.period_ns = toml_config.get_integer("ptp.period", 1'000'000);
    config.compensation_ns = toml_config.get_integer("ptp.compensation", 0);
    config.cpu_affinity = static_cast<int>(toml_config.get_integer("ptp.cpu", 3));
    config.enable_realtime = !toml_config.get_boolean("ptp.no-rt", false);

    // Sampling parameters (use defaults, allow override)
    config.sampling.sample_hz = static_cast<int>(toml_config.get_integer("ptp.sample-hz", 500));
    config.sampling.window_size = static_cast<int>(toml_config.get_integer("ptp.window-size", 256));
    config.sampling.min_samples_for_healthy = static_cast<int>(toml_config.get_integer("ptp.min-samples", 32));

    // Behavior
    config.verbose = !toml_config.get_boolean("ptp.quiet", false);
    config.lock_memory = !toml_config.get_boolean("ptp.no-mlock", false);

    return config;
}

}  // namespace statusbar::ptpclient
