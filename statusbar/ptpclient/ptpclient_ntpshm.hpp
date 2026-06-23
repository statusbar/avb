#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// @file ptpclient_ntpshm.hpp
/// @brief PtpClientBase driver backed by the Linux NTP shared-memory reader.
///
/// Adapts @c gptp::NtpShmReader (the raw NTP SHM clock source) to the
/// @c ptpclient::PtpClientBase interface so NTP SHM can be used as a drop-in
/// PTP clock source in the @c create_client / @c setup_ptp pipeline.
///
/// The driver lives in the @c ptpclient module (not @c gptp) because it depends
/// on @c ptpclient::PtpClientBase; @c gptp owns only the raw SHM reader. This
/// keeps the dependency one-directional (ptpclient -> gptp).
///
/// Usage with PtpTimeBridge:
/// @code
///   auto client = std::make_unique<NtpShmPtpClient>();
///   client->open("0");  // segment number as string
///   bridge.start_sampling(*client);
/// @endcode

#if defined(__linux__)

#    include "statusbar/gptp/gptp_ntpshm.hpp"
#    include "statusbar/ptpclient/ptpclient_base.hpp"
#    include "statusbar/status/status.hpp"

#    include <cstdint>
#    include <memory>
#    include <string>
#    include <string_view>

namespace statusbar::ptpclient {

/// PtpClientBase adapter for @c gptp::NtpShmReader.
///
/// Allows NTP SHM to be used as a drop-in PTP clock source in the
/// existing @c create_client / @c setup_ptp pipeline. The @c open() method
/// accepts a segment index as a string (e.g. "0") and creates an internal
/// @c gptp::NtpShmReader.
class NtpShmPtpClient : public PtpClientBase
{
  public:
    NtpShmPtpClient() = default;
    ~NtpShmPtpClient() noexcept override = default;

    NtpShmPtpClient(NtpShmPtpClient const&) = delete;
    auto operator=(NtpShmPtpClient const&) -> NtpShmPtpClient& = delete;
    NtpShmPtpClient(NtpShmPtpClient&&) noexcept = default;
    auto operator=(NtpShmPtpClient&&) noexcept -> NtpShmPtpClient& = default;

    /// Open the NTP SHM segment
    /// @param device_path Segment index as string (e.g., "0", "1"); default "0"
    [[nodiscard]] auto open(std::string_view device_path) noexcept -> statusbar::Status override;

    void close() noexcept override { reader_.reset(); }

    [[nodiscard]] auto is_open() const noexcept -> bool override { return reader_ != nullptr; }

    [[nodiscard]] auto get_time_ns() const noexcept -> statusbar::StatusValue<int64_t> override
    {
        if (!reader_) {
            return statusbar::failure(PtpError::device_not_open);
        }
        return reader_->get_ptp_time_ns();
    }

    [[nodiscard]] auto device_path() const noexcept -> std::string_view override { return device_name_; }

  private:
    std::unique_ptr<gptp::NtpShmReader> reader_;
    std::string device_name_{"ntpshm"};
};

}  // namespace statusbar::ptpclient

#endif  // __linux__
