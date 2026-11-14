// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// gptp_read_ntpshm_tool — Read PTP time from linuxptp's NTP SHM segment
//
// Usage: statusbar-gptp-ntpshm [segment]
//   segment: SHM segment index (default: 0, key NTP0 = 0x4E545030)
//   --help:  print the usage
//
// Requires ptp4l running with: clock_servo ntpshm

#if defined(__linux__)

#    include "gptp_ntpshm.hpp"

#    include <cstdlib>
#    include <print>
#    include <string_view>

using namespace statusbar::gptp;

namespace {

auto wants_help(int argc, char** argv) -> bool
{
    for (int i = 1; i < argc; ++i) {
        std::string_view const a = argv[i];
        if (a == "--help" || a == "-h") {
            return true;
        }
    }
    return false;
}

void print_usage(FILE* out, char const* prog)
{
    std::println(out, "Usage: {} [segment]", prog);
    std::println(out, "");
    std::println(out, "Read the last PTP sample and the current PTP time from linuxptp's NTP SHM");
    std::println(out, "segment (ptp4l with clock_servo ntpshm). segment is the SHM segment index,");
    std::println(out, "default 0 (key NTP0 = 0x{:08x}). --help prints this.", NTPSHM_KEY_BASE);
}

void print_times(NtpShmSample const& sample, int64_t const ptp_now_ns)
{
    int64_t const offset_ns = sample.local_ns - sample.ptp_ns;
    std::println("Last sample PTP time:   {}.{:09}", sample.ptp_ns / NS_PER_SEC, sample.ptp_ns % NS_PER_SEC);
    std::println("Last sample local time: {}.{:09}", sample.local_ns / NS_PER_SEC, sample.local_ns % NS_PER_SEC);
    std::println("Offset (local - PTP):   {} ns", offset_ns);
    std::println("Estimated PTP now:      {}.{:09}", ptp_now_ns / NS_PER_SEC, ptp_now_ns % NS_PER_SEC);
}

auto report(NtpShmReader& reader) -> int
{
    auto sample_result = reader.read_sample();
    if (!sample_result) {
        std::println(stderr, "No valid sample available.");
        return 1;
    }
    auto ptp_result = reader.get_ptp_time_ns();
    if (!ptp_result) {
        std::println(stderr, "Failed to get PTP time.");
        return 1;
    }
    print_times(*sample_result, *ptp_result);
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    if (wants_help(argc, argv)) {
        print_usage(stdout, argv[0]);
        return 0;
    }
    int const segment = (argc > 1) ? std::atoi(argv[1]) : 0;
    auto reader_result = NtpShmReader::open(segment);
    if (!reader_result) {
        std::println(
            stderr, "SHM segment 0x{:08x} not found. Is ptp4l running with clock_servo ntpshm?", NTPSHM_KEY_BASE + segment);
        return 1;
    }
    return report(*reader_result);
}

#else

#    include <print>

int main()
{
    std::println(stderr, "gptp_read_ntpshm_tool is only supported on Linux.");
    return 1;
}

#endif
