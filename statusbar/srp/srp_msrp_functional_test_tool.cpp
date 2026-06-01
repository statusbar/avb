// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

//
// MSRP / MVRP Functional Test Tool
//
// Deterministic offline harness: read a pcap(ng) capture, replay the
// frames whose source MAC matches --peer-mac into an MsrpParticipant
// and MvrpParticipant, and write any PDUs those state machines emit
// to an output pcap. Simulated time is driven by the pcap timestamps;
// timers are advanced to their next deadline in between input frames
// so the run is fully reproducible.
//
// Optional --expectations=<file.toml> declares a set of observer
// events that must fire during the run; the tool exits non-zero if
// any expectation is unmatched.
//
// Usage examples:
//   msrp_functional_test_tool \
//       --input=standards/msrp/example_msrp_capture.pcapng \
//       --peer-mac=a8:20:66:3c:f8:6e \
//       --our-mac=11:22:33:44:55:66 \
//       --output=out.pcap
//

#include "statusbar/args/args.hpp"
#include "statusbar/buffer/span_utils.hpp"
#include "statusbar/config/config.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/pcap/pcap.hpp"
#include "statusbar/sm/sm.hpp"
#include "statusbar/srp/srp_msrp.hpp"
#include "statusbar/srp/srp_msrp_participant.hpp"
#include "statusbar/srp/srp_mvrp.hpp"
#include "statusbar/srp/srp_mvrp_participant.hpp"
#include "statusbar/toml/toml.hpp"
#include "statusbar/tsn/tsn.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using statusbar::sm::TimePoint;
using namespace statusbar;

//
// Config
//

struct ToolConfig
{
    std::string input_path;
    std::string output_path;
    ieee::Eui48 peer_mac{};
    ieee::Eui48 our_mac{0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    std::string expectations_path;
    uint16_t vlan_id{2};
    bool verbose{true};
    /// Grace time at end of the pcap to drain timers (ms).
    int grace_ms{2000};
    srp::msrp::MsrpConfig msrp{};
    srp::mvrp::MvrpConfig mvrp{};
};

auto build_arg_specs(ToolConfig& c) -> args::ArgumentSpecs
{
    args::ArgumentSpecs specs;
    specs.add<std::string>("input", "Input pcap or pcapng file", c.input_path, [&](auto v) { c.input_path = v; });
    specs.add<std::string>("output", "Output pcap file (optional)", c.output_path, [&](auto v) { c.output_path = v; });
    specs.add<ieee::Eui48>(
        "peer-mac", "Peer MAC address (source-MAC filter for ingress frames)", c.peer_mac, [&](auto v) { c.peer_mac = v; });
    specs.add<ieee::Eui48>("our-mac", "Our synthetic endpoint's MAC address", c.our_mac, [&](auto v) { c.our_mac = v; });
    specs.add<std::string>(
        "expectations", "Optional expectations TOML file (sets non-zero exit on mismatch)", c.expectations_path, [&](auto v) {
            c.expectations_path = v;
        });
    specs.add<uint16_t>(
        "vlan-id", "VLAN ID for output frames (informational; not inserted)", c.vlan_id, [&](auto v) { c.vlan_id = v; });
    specs.add<int>(
        "grace-ms", "Grace time after last input frame, in ms, to drain timers", c.grace_ms, [&](auto v) { c.grace_ms = v; });
    specs.add<bool>("verbose", "Print per-event log to stderr", c.verbose, [&](auto v) { c.verbose = v; });
    return specs;
}

//
// Observed events recorded during the run.
//

struct ObservedEvent
{
    double at_ms{0.0};
    std::string kind;
    std::string stream_id;  // empty if N/A
    std::string detail;     // e.g. substate or sr_class
    bool matched_by_expectation{false};
};

//
// Expectations loaded from TOML.
//

struct Expectation
{
    std::string kind;
    std::string stream_id;  // empty = match any
    std::string detail;     // empty = match any (substring match)
    double at_ms{0.0};
    double tolerance_ms{10.0};
};

auto read_string_field(toml::Table const& tbl, std::string_view key, std::string& out) -> void
{
    auto const* v = tbl.get(key);
    if (v == nullptr) {
        return;
    }
    if (auto s = v->as_string(); s.has_value()) {
        out = *s;
    }
}

auto read_ms_field(toml::Table const& tbl, std::string_view key, double& out) -> void
{
    auto const* v = tbl.get(key);
    if (v == nullptr) {
        return;
    }
    if (auto f = v->as_float(); f.has_value()) {
        out = *f;
    } else if (auto n = v->as_integer(); n.has_value()) {
        out = static_cast<double>(*n);
    }
}

auto load_expectations(std::string const& path) -> std::optional<std::vector<Expectation>>
{
    auto parsed = toml::Parser::parse_file(path);
    if (!parsed.has_value()) {
        std::print(stderr, "failed to parse expectations: {}\n", parsed.error().message());
        return std::nullopt;
    }
    std::vector<Expectation> out;
    auto const* expect_v = parsed->get("expect");
    if (expect_v == nullptr) {
        return out;
    }
    auto const* arr = expect_v->as_array();
    if (arr == nullptr) {
        std::print(stderr, "expectations: 'expect' must be an array of tables\n");
        return std::nullopt;
    }
    for (size_t i = 0; i < arr->size(); ++i) {
        auto const& item = (*arr)[i];
        auto const* tbl = item.as_table();
        if (tbl == nullptr) {
            std::print(stderr, "expectations[{}]: must be a table\n", i);
            return std::nullopt;
        }
        Expectation e;
        read_string_field(*tbl, "kind", e.kind);
        read_string_field(*tbl, "stream_id", e.stream_id);
        read_string_field(*tbl, "detail", e.detail);
        read_ms_field(*tbl, "at_ms", e.at_ms);
        read_ms_field(*tbl, "tolerance_ms", e.tolerance_ms);
        if (e.kind.empty()) {
            std::print(stderr, "expectations[{}]: 'kind' is required\n", i);
            return std::nullopt;
        }
        out.push_back(std::move(e));
    }
    return out;
}

//
// Time conversion helpers.
//

auto ms_since(TimePoint base, TimePoint t) -> double
{
    auto const ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t - base).count();
    return static_cast<double>(ns) / 1e6;
}

//
// Advance simulated time up to `target`, firing participant tick()s at
// their next deadlines along the way.
//

template <class Msrp, class Mvrp>
auto advance_to(TimePoint now, TimePoint target, Msrp& msrp, Mvrp& mvrp) -> TimePoint
{
    while (now < target) {
        auto next = target;
        auto const d_msrp = msrp.next_deadline();
        if (d_msrp != TimePoint::max() && d_msrp < next) {
            next = d_msrp;
        }
        auto const d_mvrp = mvrp.next_deadline();
        if (d_mvrp != TimePoint::max() && d_mvrp < next) {
            next = d_mvrp;
        }
        if (next <= now) {
            next = now;
        }
        now = next;
        msrp.tick(now);
        mvrp.tick(now);
        if (next == target) {
            break;
        }
    }
    return now;
}

//
// Main
//

struct RunStats
{
    size_t input_frames_total{0};
    size_t input_frames_matched{0};
    size_t msrp_pdus_emitted{0};
    size_t mvrp_pdus_emitted{0};
    size_t talker_advertise_events{0};
    size_t talker_failed_events{0};
    size_t talker_leave_events{0};
    size_t listener_events{0};
    size_t listener_leave_events{0};
    size_t domain_events{0};
    size_t vlan_register_events{0};
    size_t vlan_leave_events{0};
};

auto format_ms(double v) -> std::string
{
    return std::format("{:.3f}", v);
}

auto format_sr_class_vid(srp::msrp::DomainFirstValue const& fv) -> std::string
{
    return std::format(
        "sr_class={} pcp={} vid={}",
        static_cast<unsigned>(fv.sr_class_id.get()),
        static_cast<unsigned>(fv.sr_class_priority.get()),
        fv.sr_class_vid.get());
}

auto run(ToolConfig const& cfg) -> int
{
    auto driver_result = pcap::PcapReplayDriver::open(cfg.input_path);
    if (!driver_result) {
        std::print(stderr, "error: open pcap '{}': {}\n", cfg.input_path, driver_result.error().message());
        return EXIT_FAILURE;
    }
    auto& driver = *driver_result;

    std::optional<pcap::FileWriter> writer;
    if (!cfg.output_path.empty()) {
        auto wr = pcap::FileWriter::open(cfg.output_path);
        if (!wr) {
            std::print(stderr, "error: open output pcap '{}': {}\n", cfg.output_path, wr.error().message());
            return EXIT_FAILURE;
        }
        writer.emplace(std::move(*wr));
    }

    if (auto const v = cfg.msrp.validate(); !v) {
        std::print(stderr, "invalid MSRP config: {}\n", v.error().message());
        return EXIT_FAILURE;
    }
    srp::msrp::MsrpParticipantT<> msrp{cfg.msrp, 0xC0DEULL};
    srp::mvrp::MvrpParticipantT<> mvrp{cfg.mvrp, 0xC1DEULL};

    std::optional<TimePoint> base_time;
    auto current_time = TimePoint{};
    std::vector<ObservedEvent> events;

    auto log_event = [&](std::string kind, std::string stream_id = {}, std::string detail = {}) {
        ObservedEvent e;
        e.at_ms = base_time.has_value() ? ms_since(*base_time, current_time) : 0.0;
        e.kind = std::move(kind);
        e.stream_id = std::move(stream_id);
        e.detail = std::move(detail);
        if (cfg.verbose) {
            std::print(stderr, "[{} ms] {} {} {}\n", format_ms(e.at_ms), e.kind, e.stream_id, e.detail);
        }
        events.push_back(std::move(e));
    };

    srp::msrp::Observer msrp_obs;
    msrp_obs.on_talker_advertise = [&](srp::msrp::TalkerAdvertiseFirstValue const& fv, srp::mrp::Operation /*op*/) {
        log_event("talker_advertise", tsn::to_string(fv.stream_id));
    };
    msrp_obs.on_talker_failed = [&](srp::msrp::TalkerFailedFirstValue const& fv, srp::mrp::Operation /*op*/) {
        log_event("talker_failed", tsn::to_string(fv.advertise.stream_id));
    };
    msrp_obs.on_talker_leave = [&](tsn::StreamId const& id) { log_event("talker_leave", tsn::to_string(id)); };
    msrp_obs.on_listener = [&](tsn::StreamId const& id, srp::msrp::ListenerDeclaration decl, srp::mrp::Operation /*op*/) {
        char const* sub = "ignore";
        switch (decl) {
            case srp::msrp::ListenerDeclaration::AskingFailed:
                sub = "asking_failed";
                break;
            case srp::msrp::ListenerDeclaration::Ready:
                sub = "ready";
                break;
            case srp::msrp::ListenerDeclaration::ReadyFailed:
                sub = "ready_failed";
                break;
            default:
                sub = "ignore";
                break;
        }
        log_event("listener", tsn::to_string(id), std::string{sub});
    };
    msrp_obs.on_listener_leave = [&](tsn::StreamId const& id) { log_event("listener_leave", tsn::to_string(id)); };
    msrp_obs.on_domain = [&](srp::msrp::DomainFirstValue const& fv, srp::mrp::Operation /*op*/) {
        log_event("domain", std::string{}, format_sr_class_vid(fv));
    };
    msrp_obs.on_domain_leave = [&](srp::msrp::DomainFirstValue const& fv) {
        log_event("domain_leave", std::string{}, std::format("sr_class={}", static_cast<unsigned>(fv.sr_class_id.get())));
    };
    (void)msrp.subscribe(msrp_obs);

    srp::mvrp::Observer mvrp_obs;
    mvrp_obs.on_vlan_registered = [&](uint16_t vid, srp::mrp::Operation /*op*/) {
        log_event("vlan_registered", std::string{}, std::format("vid={}", vid));
    };
    mvrp_obs.on_vlan_leave = [&](uint16_t vid) { log_event("vlan_leave", std::string{}, std::format("vid={}", vid)); };
    (void)mvrp.subscribe(mvrp_obs);

    RunStats stats;

    auto make_send = [&](uint16_t ethertype, uint64_t mcast, size_t& counter) {
        return [&, ethertype, mcast](std::span<uint8_t const> pdu) -> bool {
            ++counter;
            if (writer.has_value()) {
                ieee::Eui48 da{};
                da.from_uint64(mcast);
                auto const ts_us = base_time.has_value()
                    ? static_cast<uint64_t>(
                          std::chrono::duration_cast<std::chrono::microseconds>(current_time - *base_time).count())
                    : 0ULL;
                (void)writer->write_packet(ts_us, da, cfg.our_mac, ethertype, pdu);
            }
            return true;
        };
    };
    msrp.set_send_pdu(make_send(srp::msrp::ETHERTYPE, srp::msrp::APPLICATION_ADDRESS, stats.msrp_pdus_emitted));
    mvrp.set_send_pdu(make_send(srp::mvrp::ETHERTYPE, srp::mvrp::APPLICATION_ADDRESS, stats.mvrp_pdus_emitted));

    msrp.start(current_time);
    mvrp.start(current_time);

    auto walk_status = driver.for_each([&](pcap::ReplayEvent const& evt) {
        if (!base_time.has_value()) {
            base_time = evt.timestamp;
        }
        current_time = advance_to(current_time, evt.timestamp, msrp, mvrp);
        if (evt.frame.src_mac != cfg.peer_mac) {
            return;
        }
        if (evt.frame.ethertype == srp::msrp::ETHERTYPE) {
            msrp.receive_pdu(evt.payload, current_time);
            ++stats.input_frames_matched;
        } else if (evt.frame.ethertype == srp::mvrp::ETHERTYPE) {
            mvrp.receive_pdu(evt.payload, current_time);
            ++stats.input_frames_matched;
        }
    });
    if (!walk_status) {
        std::print(stderr, "error: pcap read: {}\n", walk_status.error().message());
        return EXIT_FAILURE;
    }
    stats.input_frames_total = driver.stats().packets_total;

    auto const grace_tp = current_time + std::chrono::milliseconds{cfg.grace_ms};
    current_time = advance_to(current_time, grace_tp, msrp, mvrp);

    for (auto const& e : events) {
        if (e.kind == "talker_advertise") {
            ++stats.talker_advertise_events;
        } else if (e.kind == "talker_failed") {
            ++stats.talker_failed_events;
        } else if (e.kind == "talker_leave") {
            ++stats.talker_leave_events;
        } else if (e.kind == "listener") {
            ++stats.listener_events;
        } else if (e.kind == "listener_leave") {
            ++stats.listener_leave_events;
        } else if (e.kind == "domain") {
            ++stats.domain_events;
        } else if (e.kind == "vlan_registered") {
            ++stats.vlan_register_events;
        } else if (e.kind == "vlan_leave") {
            ++stats.vlan_leave_events;
        }
    }

    std::vector<Expectation> expectations;
    if (!cfg.expectations_path.empty()) {
        auto parsed = load_expectations(cfg.expectations_path);
        if (!parsed.has_value()) {
            return EXIT_FAILURE;
        }
        expectations = std::move(*parsed);
    }

    int unmatched = 0;
    for (auto const& ex : expectations) {
        bool matched = false;
        for (auto& ev : events) {
            if (ev.matched_by_expectation) {
                continue;
            }
            if (ev.kind != ex.kind) {
                continue;
            }
            if (!ex.stream_id.empty() && ev.stream_id != ex.stream_id) {
                continue;
            }
            if (!ex.detail.empty() && ev.detail.find(ex.detail) == std::string::npos) {
                continue;
            }
            auto const dt = std::abs(ev.at_ms - ex.at_ms);
            if (dt <= ex.tolerance_ms) {
                ev.matched_by_expectation = true;
                matched = true;
                break;
            }
        }
        if (!matched) {
            ++unmatched;
            std::print(
                stderr,
                "UNMATCHED EXPECTATION: kind={} stream_id={} detail={} at_ms={} (+/- {})\n",
                ex.kind,
                ex.stream_id,
                ex.detail,
                format_ms(ex.at_ms),
                format_ms(ex.tolerance_ms));
        }
    }

    std::print(stderr, "\n=== msrp_functional_test_tool summary ===\n");
    std::print(stderr, "input frames total:   {}\n", stats.input_frames_total);
    std::print(stderr, "input frames matched: {}\n", stats.input_frames_matched);
    std::print(stderr, "msrp pdus emitted:    {}\n", stats.msrp_pdus_emitted);
    std::print(stderr, "mvrp pdus emitted:    {}\n", stats.mvrp_pdus_emitted);
    std::print(
        stderr,
        "events: talker_adv={} talker_failed={} talker_leave={}\n",
        stats.talker_advertise_events,
        stats.talker_failed_events,
        stats.talker_leave_events);
    std::print(
        stderr,
        "        listener={} listener_leave={} domain={}\n",
        stats.listener_events,
        stats.listener_leave_events,
        stats.domain_events);
    std::print(stderr, "        vlan_reg={} vlan_leave={}\n", stats.vlan_register_events, stats.vlan_leave_events);
    std::print(stderr, "expectations: {} total, {} unmatched\n", expectations.size(), unmatched);

    return unmatched == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv)
{
    ToolConfig cfg;
    auto specs = build_arg_specs(cfg);

    auto const status = config::parse_cli_args(argc, argv, specs);
    if (!status) {
        if (config::handled_builtin_command(status)) {
            return EXIT_SUCCESS;
        }
        std::print(stderr, "argument error: {}\n", status.error().message());
        return EXIT_FAILURE;
    }

    if (cfg.input_path.empty()) {
        std::print(stderr, "error: --input is required\n");
        return EXIT_FAILURE;
    }
    if (cfg.peer_mac == ieee::Eui48{}) {
        std::print(stderr, "error: --peer-mac is required\n");
        return EXIT_FAILURE;
    }

#if __cpp_exceptions
    try {
#endif
        return run(cfg);
#if __cpp_exceptions
    } catch (std::exception const& e) {
        std::print(stderr, "error: {}\n", e.what());
        return EXIT_FAILURE;
    }
#endif
}
