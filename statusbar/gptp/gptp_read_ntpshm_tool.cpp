// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// gptp_read_ntpshm_tool — Read PTP time from linuxptp's NTP SHM segment
//
// Usage: gptp_read_ntpshm_tool [segment]
//   segment: SHM segment index (default: 0, key NTP0 = 0x4E545030)
//
// Requires ptp4l running with: clock_servo ntpshm

#if defined(__linux__)

#    include "gptp_ntpshm.hpp"

#    include <cstdlib>
#    include <print>

using namespace statusbar::gptp;

int main(int argc, char** argv)
{
    int const segment = (argc > 1) ? std::atoi(argv[1]) : 0;

    auto reader_result = NtpShmReader::open(segment);
    if (!reader_result) {
        std::println(
            stderr, "SHM segment 0x{:08x} not found. Is ptp4l running with clock_servo ntpshm?", NTPSHM_KEY_BASE + segment);
        return 1;
    }

    auto& reader = *reader_result;

    auto sample_result = reader.read_sample();
    if (!sample_result) {
        std::println(stderr, "No valid sample available.");
        return 1;
    }

    auto const& sample = *sample_result;
    int64_t const offset_ns = sample.local_ns - sample.ptp_ns;

    auto ptp_result = reader.get_ptp_time_ns();
    if (!ptp_result) {
        std::println(stderr, "Failed to get PTP time.");
        return 1;
    }

    int64_t const ptp_ns = *ptp_result;
    int64_t const ptp_sec = ptp_ns / NS_PER_SEC;
    int64_t const ptp_nsec = ptp_ns % NS_PER_SEC;

    std::println("Last sample PTP time:   {}.{:09}", sample.ptp_ns / NS_PER_SEC, sample.ptp_ns % NS_PER_SEC);
    std::println("Last sample local time: {}.{:09}", sample.local_ns / NS_PER_SEC, sample.local_ns % NS_PER_SEC);
    std::println("Offset (local - PTP):   {} ns", offset_ns);
    std::println("Estimated PTP now:      {}.{:09}", ptp_sec, ptp_nsec);

    return 0;
}

#else

#    include <print>

int main()
{
    std::println(stderr, "gptp_read_ntpshm_tool is only supported on Linux.");
    return 1;
}

#endif
