// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// @file gptp_ntpshm.cpp
/// @brief Implementation of NTP shared memory reader for PTP time access.

#include "statusbar/gptp/gptp_ntpshm.hpp"

#if defined(__linux__)

namespace statusbar::gptp {

auto NtpShmErrorCategory::message(int ev) const -> std::string
{
    switch (static_cast<NtpShmError>(ev)) {
        case NtpShmError::attach_failed:
            return "Failed to attach to NTP SHM segment";
        case NtpShmError::no_valid_sample:
            return "No valid sample available in NTP SHM";
        default:
            return "Unknown NTP SHM error";
    }
}

auto NtpShmReader::open(int segment) noexcept -> StatusValue<NtpShmReader>
{
    // Obtain the SysV shared memory ID for the NTP SHM segment.
    // Key is NTPSHM_KEY_BASE + segment index (e.g. 0x4E545030 for NTP0).
    int const shmid = ::shmget(NTPSHM_KEY_BASE + segment, sizeof(NtpShmSegment), 0600);
    if (shmid == -1) {
        return std::unexpected(make_error_code(NtpShmError::attach_failed));
    }
    // Attach the segment read-only into our address space.
    void* addr = ::shmat(shmid, nullptr, SHM_RDONLY);
    // NOLINTNEXTLINE(performance-no-int-to-ptr, cppcoreguidelines-pro-type-reinterpret-cast)
    if (addr == reinterpret_cast<void*>(-1)) {  // canonical MAP_FAILED sentinel
        return std::unexpected(make_error_code(NtpShmError::attach_failed));
    }
    return NtpShmReader(static_cast<NtpShmSegment const*>(addr));
}

auto NtpShmReader::operator=(NtpShmReader&& other) noexcept -> NtpShmReader&
{
    if (this != &other) {
        if (shm_ != nullptr) {
            ::shmdt(shm_);
        }
        shm_ = other.shm_;
        other.shm_ = nullptr;
    }
    return *this;
}

auto NtpShmReader::read_sample() const noexcept -> StatusValue<NtpShmSample>
{
    for (int attempt = 0; attempt < 10; ++attempt) {
        int const c1 = shm_->count;
        if (shm_->valid == 0 || (c1 & 1) != 0) {
            continue;
        }

        NtpShmSample sample;
        sample.ptp_ns = static_cast<int64_t>(shm_->clock_timestamp_sec) * NS_PER_SEC + shm_->clock_timestamp_nsec;
        sample.local_ns = static_cast<int64_t>(shm_->receive_timestamp_sec) * NS_PER_SEC + shm_->receive_timestamp_nsec;

        int const c2 = shm_->count;
        if (c2 == c1 && shm_->valid != 0) {
            return success(sample);
        }
    }
    return std::unexpected(make_error_code(NtpShmError::no_valid_sample));
}

auto NtpShmReader::get_ptp_time_ns() const noexcept -> StatusValue<int64_t>
{
    auto sample_result = read_sample();
    if (!sample_result) {
        return forward_failure(sample_result);
    }

    int64_t const offset_ns = sample_result->local_ns - sample_result->ptp_ns;

    timespec now{};
    ::clock_gettime(CLOCK_REALTIME, &now);
    int64_t const now_ns = (static_cast<int64_t>(now.tv_sec) * NS_PER_SEC) + now.tv_nsec;

    return success(now_ns - offset_ns);
}

auto NtpShmPtpClient::open(std::string_view device_path) noexcept -> statusbar::Status
{
    int segment = 0;
    if (!device_path.empty()) {
        // Parse the segment index from the device_path string (e.g. "0", "1").
        // Uses std::from_chars for locale-independent, allocation-free integer parsing.
        auto result = std::from_chars(device_path.data(), device_path.data() + device_path.size(), segment);
        if (result.ec != std::errc{}) {
            return statusbar::failure(ptpclient::PtpError::device_not_open);
        }
    }

    auto reader_result = NtpShmReader::open(segment);
    if (!reader_result) {
        return statusbar::failure(ptpclient::PtpError::device_not_open);
    }

    reader_ = std::make_unique<NtpShmReader>(std::move(*reader_result));
    device_name_ = std::string("ntpshm:") + std::string(device_path.empty() ? "0" : device_path);
    return statusbar::success();
}

}  // namespace statusbar::gptp

#endif  // __linux__
