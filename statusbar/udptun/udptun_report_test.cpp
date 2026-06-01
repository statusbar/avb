// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_report.hpp"

#include "statusbar/stats/stats_atomic_histogram.hpp"
#include "statusbar/test/test.hpp"
#include "statusbar/udptun/udptun_per_source_tracker.hpp"
#include "statusbar/udptun/udptun_stats.hpp"

#include <sstream>
#include <string>

using namespace statusbar;

namespace {

auto make_hcfg() -> stats::AtomicHistogramConfig
{
    return {.low_ns = 0, .high_ns = 1'000'000, .bin_width_ns = 1'000};
}

}  // namespace

TEST(udptun_report_live, omits_rtt_when_null)
{
    udptun::PerSourceTracker tracker{make_hcfg(), 4};
    std::ostringstream oss;
    udptun::print_live_report(oss, /*wall_seconds*/ 1.0, tracker, /*rtt*/ nullptr);
    EXPECT_TRUE(!oss.str().contains("RTT"));
}

TEST(udptun_report_live, omits_rtt_when_empty)
{
    udptun::PerSourceTracker tracker{make_hcfg(), 4};
    udptun::LatencyStats rtt{make_hcfg()};
    std::ostringstream oss;
    udptun::print_live_report(oss, 1.0, tracker, &rtt);
    EXPECT_TRUE(!oss.str().contains("RTT"));
}

TEST(udptun_report_live, emits_rtt_line_when_nonempty)
{
    udptun::PerSourceTracker tracker{make_hcfg(), 4};
    udptun::LatencyStats rtt{make_hcfg()};
    rtt.record(100'000);
    rtt.record(200'000);

    std::ostringstream oss;
    udptun::print_live_report(oss, 1.0, tracker, &rtt);
    auto const out = oss.str();
    EXPECT_TRUE(out.contains("RTT"));
    EXPECT_TRUE(out.contains("count=2"));
}

TEST(udptun_report_final, includes_rtt_histogram_when_nonempty)
{
    udptun::PerSourceTracker tracker{make_hcfg(), 4};
    udptun::LatencyStats rtt{make_hcfg()};
    rtt.record(150'000);

    std::ostringstream oss;
    udptun::print_final_summary(oss, "test", tracker, &rtt);
    auto const out = oss.str();
    EXPECT_TRUE(out.contains("RTT"));
}

TEST(udptun_report_live, formats_times_in_milliseconds)
{
    udptun::PerSourceTracker tracker{make_hcfg(), 4};
    udptun::LatencyStats rtt{make_hcfg()};
    rtt.record(450'000);  // 0.45 ms — rounds to 0.5 with %.1f

    std::ostringstream oss;
    udptun::print_live_report(oss, 1.0, tracker, &rtt);
    auto const out = oss.str();
    EXPECT_TRUE(out.contains("ms"));
    EXPECT_TRUE(!out.contains("ns "));
    EXPECT_TRUE(out.contains("0.5ms"));
}

TEST(udptun_report_live, prints_rtt_loss_when_tx_stats_given)
{
    udptun::PerSourceTracker tracker{make_hcfg(), 4};
    udptun::LatencyStats rtt{make_hcfg()};
    rtt.record(100'000);
    rtt.record(200'000);
    // sent 10 total, all of them are old enough that the grace window doesn't
    // apply (sent_at_cutoff=10), received 2 → missing = 8, loss = 80.00%.
    udptun::TxLossStats tx{.sent_total = 10, .sent_at_cutoff = 10};
    std::ostringstream oss;
    udptun::print_live_report(oss, 1.0, tracker, &rtt, &tx);
    auto const out = oss.str();
    EXPECT_TRUE(out.contains("sent=10"));
    EXPECT_TRUE(out.contains("loss=80.00%"));
    EXPECT_TRUE(out.contains("8 missing sequence ids"));
}

TEST(udptun_report_live, grace_window_hides_in_flight_packets)
{
    udptun::PerSourceTracker tracker{make_hcfg(), 4};
    udptun::LatencyStats rtt{make_hcfg()};
    for (int i = 0; i < 50; ++i) {
        rtt.record(100'000);
    }
    // 100 sent total; only 50 had been sent before the grace cutoff. The
    // other 50 are still in flight. Received 50 → missing = 0, loss = 0%.
    udptun::TxLossStats tx{.sent_total = 100, .sent_at_cutoff = 50};
    std::ostringstream oss;
    udptun::print_live_report(oss, 1.0, tracker, &rtt, &tx);
    auto const out = oss.str();
    EXPECT_TRUE(out.contains("sent=100"));
    EXPECT_TRUE(out.contains("loss=0.00%"));
    EXPECT_TRUE(out.contains("0 missing sequence ids"));
}

TEST(udptun_report_live, prints_redundancy_stats_when_given)
{
    udptun::PerSourceTracker tracker{make_hcfg(), 4};
    udptun::LatencyStats rtt{make_hcfg()};
    rtt.record(100'000);
    rtt.record(200'000);
    udptun::TxLossStats tx{.sent_total = 1000, .sent_at_cutoff = 1000};
    // Of 1000 sent, 998 received as primary, 1 recovered, 1 true loss.
    udptun::RedundancyDisplayStats red{.recovered = 1, .true_loss = 1};
    std::ostringstream oss;
    udptun::print_live_report(oss, 1.0, tracker, &rtt, &tx, &red);
    auto const out = oss.str();
    EXPECT_TRUE(out.contains("recovered=1"));
    EXPECT_TRUE(out.contains("true_loss=0.10%"));
    EXPECT_TRUE(out.contains("(1)"));
}

TEST(udptun_report_live, omits_redundancy_when_red_is_null)
{
    udptun::PerSourceTracker tracker{make_hcfg(), 4};
    udptun::LatencyStats rtt{make_hcfg()};
    rtt.record(100'000);
    udptun::TxLossStats tx{.sent_total = 100, .sent_at_cutoff = 100};
    std::ostringstream oss;
    udptun::print_live_report(oss, 1.0, tracker, &rtt, &tx, nullptr);
    auto const out = oss.str();
    EXPECT_TRUE(!out.contains("recovered="));
    EXPECT_TRUE(!out.contains("true_loss="));
}

TEST(udptun_report_live, omits_rtt_loss_when_tx_stats_null)
{
    udptun::PerSourceTracker tracker{make_hcfg(), 4};
    udptun::LatencyStats rtt{make_hcfg()};
    rtt.record(100'000);
    std::ostringstream oss;
    udptun::print_live_report(oss, 1.0, tracker, &rtt);
    auto const out = oss.str();
    EXPECT_TRUE(!out.contains("sent="));
    EXPECT_TRUE(!out.contains("missing sequence ids"));
}

TEST(udptun_report_live, includes_tx_failures_when_nonzero)
{
    udptun::PerSourceTracker tracker{make_hcfg(), 4};
    udptun::LatencyStats rtt{make_hcfg()};
    rtt.record(100'000);
    udptun::TxLossStats tx{.sent_total = 100, .sent_at_cutoff = 100, .tx_failures = 7};
    std::ostringstream oss;
    udptun::print_live_report(oss, 1.0, tracker, &rtt, &tx);
    auto const out = oss.str();
    EXPECT_TRUE(out.contains("tx_failures=7"));
}

TEST(udptun_report_live, omits_tx_failures_when_zero)
{
    udptun::PerSourceTracker tracker{make_hcfg(), 4};
    udptun::LatencyStats rtt{make_hcfg()};
    rtt.record(100'000);
    udptun::TxLossStats tx{.sent_total = 100, .sent_at_cutoff = 100, .tx_failures = 0};
    std::ostringstream oss;
    udptun::print_live_report(oss, 1.0, tracker, &rtt, &tx);
    auto const out = oss.str();
    EXPECT_TRUE(!out.contains("tx_failures"));
}

TEST(udptun_report_live, appends_gptp_ns_to_eui64_line_when_nonzero)
{
    udptun::PerSourceTracker tracker{make_hcfg(), 4};
    ieee::Eui64 const sender{0x88, 0xa2, 0x9e, 0x00, 0x00, 0x82, 0x82, 0xee};
    (void)tracker.observe(sender, /*seq=*/1, /*latency_ns=*/200'000, /*rx_gptp_ns=*/0, /*interval_us=*/100);

    constexpr int64_t gptp_ns = 1'778'441'877'230'894'915LL;
    std::ostringstream oss;
    udptun::print_live_report(oss, 1.0, tracker, /*rtt=*/nullptr, /*tx=*/nullptr, /*red=*/nullptr, gptp_ns);
    auto const out = oss.str();
    EXPECT_TRUE(out.contains("eui64=88:a2:9e:00:00:82:82:ee"));
    EXPECT_TRUE(out.contains("gptp_ns=1778441877230894915"));
}

TEST(udptun_report_live, omits_gptp_ns_when_zero)
{
    udptun::PerSourceTracker tracker{make_hcfg(), 4};
    ieee::Eui64 const sender{0x88, 0xa2, 0x9e, 0x00, 0x00, 0x82, 0x82, 0xee};
    (void)tracker.observe(sender, /*seq=*/1, /*latency_ns=*/200'000, /*rx_gptp_ns=*/0, /*interval_us=*/100);

    std::ostringstream oss;
    udptun::print_live_report(oss, 1.0, tracker, /*rtt=*/nullptr, /*tx=*/nullptr, /*red=*/nullptr, /*gptp_ns=*/0);
    EXPECT_TRUE(!oss.str().contains("gptp_ns="));
}

TEST(udptun_report_final, surfaces_truncated_alongside_dropped_invalid)
{
    udptun::PerSourceTracker tracker{make_hcfg(), 4};
    tracker.increment_dropped_invalid();
    tracker.increment_truncated();
    tracker.increment_truncated();
    std::ostringstream oss;
    udptun::print_final_summary(oss, "test", tracker);
    auto const out = oss.str();
    EXPECT_TRUE(out.contains("dropped_invalid: 1"));
    EXPECT_TRUE(out.contains("rx_truncated: 2"));
}

TEST_MAIN(statusbar_udptun, udptun_report_test)
