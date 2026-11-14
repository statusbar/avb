// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// gps_ratio_tracker: sample the local PHC (switch gPTP time) against GPS
// (CLOCK_REALTIME, chrony-disciplined to the site GPS grandmaster) and track
// r = switch_rate / GPS_rate using the ptpclient frequency-ratio estimators.
// Feeds the GPS-rate media-clock generator. See avb/docs/GPS_MEDIA_CLOCK.md.
//
// Linux-only (reads /dev/ptp0 via its dynamic POSIX clock-id, bracketed by
// CLOCK_REALTIME). Pick the estimator with --filter ols|kalman.
//
//   sudo statusbar-gps-ratio-tracker --device /dev/ptp0 --filter kalman
//   sudo statusbar-gps-ratio-tracker --filter ols --window-s 900
//   statusbar-gps-ratio-tracker --help

#if defined(__linux__)

#    include "statusbar/ptpclient/ptpclient_freq_ratio.hpp"

#    include <fcntl.h>
#    include <unistd.h>

#    include <cstdint>
#    include <cstdio>
#    include <cstdlib>
#    include <ctime>
#    include <optional>
#    include <string>
#    include <string_view>

using statusbar::ptpclient::KalmanRatioTracker;
using statusbar::ptpclient::OlsRatioTracker;
using statusbar::ptpclient::RatioEstimate;

namespace {

clockid_t fd_to_clockid(int fd)
{
    return static_cast<clockid_t>((~static_cast<unsigned>(fd) << 3) | 3u);
}
std::int64_t to_i64(timespec const& t)
{
    return (static_cast<std::int64_t>(t.tv_sec) * 1'000'000'000LL) + t.tv_nsec;
}

struct Reading
{
    std::int64_t offset_ns;  // PHC - GPS
    timespec gps;            // CLOCK_REALTIME bracket midpoint (for dt)
};

// Bracket the PHC read with two CLOCK_REALTIME reads; midpoint REALTIME so PHC
// and GPS are aligned to within the (~us) bracket window.
std::optional<Reading> read_sample(int fd, clockid_t phc_clk)
{
    timespec g1{}, p{}, g2{};
    clock_gettime(CLOCK_REALTIME, &g1);
    if (clock_gettime(phc_clk, &p) != 0) {
        return std::nullopt;
    }
    clock_gettime(CLOCK_REALTIME, &g2);
    std::int64_t const mid = (to_i64(g1) + to_i64(g2)) / 2;
    timespec gps{.tv_sec = static_cast<time_t>(mid / 1'000'000'000LL), .tv_nsec = static_cast<long>(mid % 1'000'000'000LL)};
    return Reading{.offset_ns = to_i64(p) - mid, .gps = gps};
}

double dt_seconds(timespec const& now, timespec const& prev)
{
    return static_cast<double>(now.tv_sec - prev.tv_sec) + (static_cast<double>(now.tv_nsec - prev.tv_nsec) * 1e-9);
}

struct Options
{
    std::string dev = "/dev/ptp0";
    std::string filter = "kalman";
    double report_s = 5.0;
    double sample_hz = 4.0;
    double fs = 96000.0;
    double window_s = 900.0;
    KalmanRatioTracker::Config kc;
};

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
    std::fprintf(
        out,
        "Usage: %s [--flag value ...]\n"
        "\n"
        "Track r = switch_gPTP_rate / GPS_rate: the local PHC against CLOCK_REALTIME\n"
        "(GPS-disciplined), with the ptpclient frequency-ratio estimators. Prints an\n"
        "estimate every --report-s seconds. Linux-only; reads the PHC through /dev/ptpN.\n"
        "\n"
        "  --device PATH        PHC device (default /dev/ptp0)\n"
        "  --filter ols|kalman  estimator (default kalman)\n"
        "  --report-s S         seconds between reports (default 5)\n"
        "  --sample-hz HZ       PHC/GPS sample rate (default 4)\n"
        "  --fs HZ              media sample rate the estimate is quoted at (default 96000)\n"
        "  --window-s S         OLS window length in seconds (default 900)\n"
        "  --meas-noise-ns NS   Kalman measurement noise, one sigma\n"
        "  --jerk-psd PSD       Kalman frequency-drift process noise\n"
        "  --gate SIGMAS        Kalman innovation gate\n"
        "  --help               this text\n",
        prog);
}

// The second half of the option set: the Kalman estimator's own knobs.
void parse_kalman_option(std::string const& a, char const* value, KalmanRatioTracker::Config& kc)
{
    if (a == "--meas-noise-ns") {
        kc.meas_noise_ns = atof(value);
    } else if (a == "--jerk-psd") {
        kc.jerk_psd = atof(value);
    } else if (a == "--gate") {
        kc.gate_sigmas = atof(value);
    }
}

void parse_option(std::string const& a, char const* value, Options& o)
{
    if (a == "--device") {
        o.dev = value;
    } else if (a == "--filter") {
        o.filter = value;
    } else if (a == "--report-s") {
        o.report_s = atof(value);
    } else if (a == "--sample-hz") {
        o.sample_hz = atof(value);
    } else if (a == "--fs") {
        o.fs = atof(value);
    } else if (a == "--window-s") {
        o.window_s = atof(value);
    } else {
        parse_kalman_option(a, value, o.kc);
    }
}

// Space-separated `--flag value` pairs; a flag is read with the value after it.
auto parse_options(int argc, char** argv) -> Options
{
    Options o;
    for (int i = 1; i < argc - 1; ++i) {
        parse_option(argv[i], argv[i + 1], o);
    }
    return o;
}

void print_estimate(RatioEstimate const& e, bool const use_kalman, double const fs)
{
    printf(
        "[%s] r=%.9f (%+0.4f ppm)  f_unc=%.3f ppb  drift=%+0.4f ppm/hr  "
        "PHC-GPS=%lld ns  avtp_ns/sample@%.0f=%.4f\n",
        use_kalman ? "kalman" : "ols",
        e.r,
        e.ppm(),
        e.freq_uncertainty_ppb,
        e.drift_ppm_per_hr,
        static_cast<long long>(e.offset_ns),
        fs,
        e.phc_ns_per_sample(fs));
    fflush(stdout);
}

// One run of the tracker: samples the PHC against GPS forever, reporting on schedule.
class RatioRun
{
  public:
    RatioRun(Options const& o, int const fd)
        : o_(o)
        , fd_(fd)
        , phc_clk_(fd_to_clockid(fd))
        , use_kalman_(o.filter != "ols")
        , ols_{o.window_s}
        , kf_{o.kc}
    {}

    [[noreturn]] void run()
    {
        for (;;) {
            if (auto s = read_sample(fd_, phc_clk_)) {
                ingest(*s);
            }
            timespec ts{.tv_sec = 0, .tv_nsec = static_cast<long>(1e9 / o_.sample_hz)};
            clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
        }
    }

  private:
    void ingest(Reading const& s)
    {
        double const dt = prev_gps_ ? dt_seconds(s.gps, *prev_gps_) : 0.0;
        prev_gps_ = s.gps;
        if (use_kalman_) {
            kf_.add(s.offset_ns, dt);
        } else {
            ols_.add(s.offset_ns, dt);
        }
        since_report_ += 1.0 / o_.sample_hz;
        if (first_ || since_report_ >= o_.report_s) {
            report();
        }
    }

    void report()
    {
        first_ = false;
        since_report_ = 0;
        RatioEstimate const e = use_kalman_ ? kf_.estimate() : ols_.estimate();
        if (e.valid) {
            print_estimate(e, use_kalman_, o_.fs);
        }
    }

    Options o_;
    int fd_;
    clockid_t phc_clk_;
    bool use_kalman_;
    OlsRatioTracker ols_;
    KalmanRatioTracker kf_;
    std::optional<timespec> prev_gps_;
    double since_report_ = 0;
    bool first_ = true;
};

}  // namespace

int main(int argc, char** argv)
{
    if (wants_help(argc, argv)) {
        print_usage(stdout, argv[0]);
        return 0;
    }
    Options const o = parse_options(argc, argv);
    // sample_hz drives 1e9/sample_hz (sleep) and 1.0/sample_hz (report accumulator);
    // a non-positive value yields inf/an invalid tv_nsec, so the sleep returns EINVAL
    // and the loop spins. Reject it up front.
    if (o.sample_hz <= 0.0) {
        std::fprintf(stderr, "Error: --sample-hz must be positive\n");
        return 1;
    }
    int const fd = open(o.dev.c_str(), O_RDWR);
    if (fd < 0) {
        perror(("open " + o.dev).c_str());
        return 1;
    }
    RatioRun{o, fd}.run();
}

#else

#    include <print>

int main()
{
    std::println(stderr, "statusbar-gps-ratio-tracker is only supported on Linux.");
    return 1;
}

#endif
