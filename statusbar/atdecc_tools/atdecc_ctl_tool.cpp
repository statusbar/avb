// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// statusbar-atdecc-ctl — scriptable (non-interactive) ATDECC controller.
//
// A thin command front-end over ControllerSimple: discovers entities via ADP,
// resolves NAMES to entity IDs (or accepts raw EUI-64s), and drives ACMP
// connect/disconnect and AEM set/get-clock-source. Supports a TOML batch file
// so many operations run in one process (one discovery pass), instead of
// spawning one controller per connection.
//
// Commands:
//   --command=list            [--entity NAME]            list entities (or one's clocks)
//   --command=connect         --talker NAME:UID --listener NAME:UID
//   --command=disconnect      --talker NAME:UID --listener NAME:UID
//   --command=set-clock-source --entity NAME --clock-source S [--clock-domain D]
//   --command=get-clock-source --entity NAME             [--clock-domain D]
//   --command=batch           --file ops.toml
//   --command=supervise       --file ops.toml  [--interval-ms N] [--watch-ms N]
//                             self-heal: reset stale streams (our local end's
//                             FRAMES_RX/FRAMES_TX counter not advancing)

#include "statusbar/atdecc/atdecc.hpp"
#include "statusbar/atdecc_tools/atdecc_controller_simple.hpp"
#include "statusbar/atdecc_tools/atdecc_ctl_ops.hpp"
#include "statusbar/atdecc_tools/atdecc_tools_common.hpp"
#include "statusbar/avtp/avtp.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/itc/itc_stop_token.hpp"
#include "statusbar/net/net_message_reactor.hpp"
#include "statusbar/net/net_posix_util.hpp"
#include "statusbar/net/net_rawnet.hpp"
#include "statusbar/toml/toml.hpp"

#include <chrono>
#include <cstdint>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace statusbar;
using namespace statusbar::atdecc;
using namespace statusbar::ieee;
using namespace statusbar::net;

using atdecc_tools::ControllerSimple;
using atdecc_tools::EntityDisplayInfo;
using atdecc_tools::Op;
using atdecc_tools::OpKind;

auto const g_start_time = std::chrono::steady_clock::now();
auto elapsed_ns() -> int64_t
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - g_start_time).count();
}

struct Config
{
    std::string command;  // list|connect|disconnect|set-clock-source|get-clock-source|batch
    std::string interface_name;
    std::string talker;    // NAME:UID
    std::string listener;  // NAME:UID
    std::string entity;    // NAME (clock-source target / list detail)
    std::string file;      // batch TOML path
    int64_t discover_ms{1500};
    int64_t clock_domain{0};
    int64_t clock_source{0};
    int64_t interval_ms{700};  // supervise: gap between the two counter samples
    int64_t watch_ms{0};       // supervise: >0 => loop forever, sleeping this long between passes
    bool once{true};           // supervise: single pass (default); watch-ms>0 overrides
    bool trace{false};         // connect/disconnect: print a per-leg ACMP handshake verdict
};

auto build_arg_specs(Config& config) -> args::ArgumentSpecs
{
    args::ArgumentSpecs specs;
    specs.add_choice(
        "command",
        "Action: list, validate, connect, disconnect, get-rx-state, get-tx-state, set-clock-source, get-clock-source, "
        "batch, supervise",
        {"list",
         "validate",
         "connect",
         "disconnect",
         "get-rx-state",
         "get-tx-state",
         "set-clock-source",
         "get-clock-source",
         "batch",
         "supervise"},
        "list",
        [&](auto v) { config.command = std::string{v}; });
    specs.add_device("interface", "Network interface (e.g., eth0)", "", [&](auto v) { config.interface_name = std::string{v}; });
    specs.add<std::string>(
        "talker", "Talker endpoint NAME:UID (connect/disconnect)", "", [&](auto v) { config.talker = std::string{v}; });
    specs.add<std::string>(
        "listener", "Listener endpoint NAME:UID (connect/disconnect)", "", [&](auto v) { config.listener = std::string{v}; });
    specs.add<std::string>(
        "entity", "Target entity NAME (clock-source / list detail)", "", [&](auto v) { config.entity = std::string{v}; });
    specs.add<std::string>(
        "file", "Batch/supervise TOML file (command=batch|supervise)", "", [&](auto v) { config.file = std::string{v}; });
    specs.add<int64_t>("discover-ms", "Discovery window in ms", 1500, [&](auto v) { config.discover_ms = v; });
    specs.add<int64_t>("clock-domain", "CLOCK_DOMAIN index (clock-source ops)", 0, [&](auto v) { config.clock_domain = v; });
    specs.add<int64_t>("clock-source", "CLOCK_SOURCE index (set-clock-source)", 0, [&](auto v) { config.clock_source = v; });
    specs.add<int64_t>(
        "interval-ms", "supervise: gap between the two counter samples (ms)", 700, [&](auto v) { config.interval_ms = v; });
    specs.add<int64_t>("watch-ms", "supervise: if >0, loop forever sleeping this long between passes (ms)", 0, [&](auto v) {
        config.watch_ms = v;
    });
    specs.add_flag("once", "supervise: run a single pass (default; ignored when --watch-ms>0)", [&](auto v) { config.once = v; });
    specs.add_flag(
        "trace", "connect/disconnect: print a per-leg ACMP handshake verdict (RX -> relay -> talker -> listener)", [&](auto v) {
            config.trace = v;
        });
    return specs;
}

void print_usage(char const* prog, args::ArgumentSpecs const& specs)
{
    std::println(stderr, "Usage: {} --interface=<name> --command=<cmd> [options]", prog);
    std::println(stderr, "\nScriptable ATDECC controller (connect / clock-source / discovery / batch).\n");
    std::println(stderr, "Options:");
    std::string help;
    specs.format_help_to(std::back_inserter(help));
    std::print(stderr, "{}", help);
    std::println(stderr, "\nExamples:");
    std::println(stderr, "  {} --interface=eth0 --command=list", prog);
    std::println(stderr, "  {} --interface=eth0 --command=connect --talker=jdk01a:0 --listener=jdk01d:0", prog);
    std::println(stderr, "  {} --interface=eth0 --command=set-clock-source --entity=the audio interface --clock-source=1", prog);
    std::println(stderr, "  {} --interface=eth0 --command=batch --file=ops.toml", prog);
    std::println(stderr, "  {} --interface=eth0 --command=supervise --file=ops.toml", prog);
}

// Run the reactor for up to `budget_ms`, invoking `on_event` for every drained
// event, until `done()` returns true or the budget elapses. Returns done().
template <typename DoneFn, typename EventFn>
auto pump(MessageReactor& reactor, ControllerSimple& ctrl, int64_t budget_ms, DoneFn done, EventFn on_event) -> bool
{
    int64_t const deadline = elapsed_ns() + (budget_ms * 1'000'000);
    while (elapsed_ns() < deadline) {
        reactor.poll_once(20);
        for (auto const& ev : ctrl.drain_events()) {
            on_event(ev);
        }
        if (done()) {
            return true;
        }
    }
    return false;
}

// Resolve a NAME (or EUI-64) against the current discovery snapshot, printing an
// error on failure.
auto resolve_or_report(std::string_view name, std::vector<EntityDisplayInfo> const& snap) -> std::optional<Eui64>
{
    std::string err;
    auto id = atdecc_tools::resolve_entity(name, snap, err);
    if (!id) {
        std::println(stderr, "Error: {}", err);
    }
    return id;
}

// Print the leg-by-leg verdict for a (dis)connect handshake. `done` is whether
// the high-level success criterion was met; the legs explain WHY when it wasn't.
void print_trace_verdict(Op const& op, bool is_connect, atdecc_tools::HandshakeLegs const& legs, bool done)
{
    char const* const relay_name = is_connect ? "CONNECT_TX_COMMAND" : "DISCONNECT_TX_COMMAND";
    char const* const tresp_name = is_connect ? "CONNECT_TX_RESPONSE" : "DISCONNECT_TX_RESPONSE";
    char const* const lresp_name = is_connect ? "CONNECT_RX_RESPONSE" : "DISCONNECT_RX_RESPONSE";
    char const* const rx_name = is_connect ? "CONNECT_RX_COMMAND" : "DISCONNECT_RX_COMMAND";

    std::println(
        "  ACMP handshake  talker {}:{}  listener {}:{}",
        op.talker.name,
        op.talker.unique_id,
        op.listener.name,
        op.listener.unique_id);
    std::println("    [1] controller -> listener   {:<22} sent", rx_name);
    std::println("    [2] listener   -> talker     {:<22} {}", relay_name, legs.relay_seen ? "relayed" : "NOT SEEN");
    std::println(
        "    [3] talker     -> listener   {:<22} {}", tresp_name, legs.talker_status ? acmp_status_name(*legs.talker_status) : "NO REPLY");
    std::println(
        "    [4] listener   -> controller {:<22} {}",
        lresp_name,
        legs.listener_status ? acmp_status_name(*legs.listener_status) : "NO REPLY");

    // Pinpoint the first leg that broke -- this is the actionable verdict.
    std::string verdict;
    switch (atdecc_tools::classify_handshake(legs)) {
        case atdecc_tools::HandshakeFault::ListenerNoRelay:
            verdict = std::format(
                "listener '{}' never relayed {} -- it did not accept/forward our command (wrong "
                "listener id/uid, or the LISTENER entity is the fault)",
                op.listener.name,
                relay_name);
            break;
        case atdecc_tools::HandshakeFault::TalkerNoReply:
            verdict = std::format(
                "listener relayed to the talker but talker '{}' never answered -- TALKER-side (wrong "
                "talker uid :{}, or the talker is silent/declining, e.g. third-party devices)",
                op.talker.name,
                op.talker.unique_id);
            break;
        case atdecc_tools::HandshakeFault::TalkerRejected:
            verdict = std::format("talker '{}' REJECTED the stream: {}", op.talker.name, acmp_status_name(*legs.talker_status));
            break;
        case atdecc_tools::HandshakeFault::ListenerNoReply:
            verdict =
                "talker answered SUCCESS but the listener's response to the controller was not seen "
                "(likely just missed on the bus -- the connection is probably up)";
            break;
        case atdecc_tools::HandshakeFault::ListenerRejected:
            verdict =
                std::format("listener '{}' returned {} to the controller", op.listener.name, acmp_status_name(*legs.listener_status));
            break;
        case atdecc_tools::HandshakeFault::None:
            verdict = "all four legs completed with SUCCESS";
            break;
    }
    std::println("    verdict: {}{}", verdict, (done ? "" : "  [unconfirmed]"));
}

// Execute a single op. Returns true on success. When `trace` is set (connect/
// disconnect only), prints a per-leg ACMP handshake verdict for diagnosis.
auto execute_op(MessageReactor& reactor, ControllerSimple& ctrl, Op const& op, bool trace = false) -> bool
{
    auto const snap = ctrl.get_display_entities();
    using atdecc_tools::AcmpTraceEvent;
    using atdecc_tools::ConnectionAddedEvent;
    using atdecc_tools::ConnectionRemovedEvent;
    using atdecc_tools::ControllerAction;
    using atdecc_tools::ControllerActionKind;
    using atdecc_tools::HandshakeLegs;
    using atdecc_tools::StatusChangedEvent;

    switch (op.kind) {
        case OpKind::Connect:
        case OpKind::Disconnect: {
            auto const tid = resolve_or_report(op.talker.name, snap);
            auto const lid = resolve_or_report(op.listener.name, snap);
            if (!tid || !lid) {
                return false;
            }
            bool const is_connect = (op.kind == OpKind::Connect);
            ControllerAction action{};
            action.kind = is_connect ? ControllerActionKind::ConnectStream : ControllerActionKind::DisconnectStream;
            action.request.talker_entity_id = *tid;
            action.request.talker_unique_id = op.talker.unique_id;
            action.request.listener_entity_id = *lid;
            action.request.listener_unique_id = op.listener.unique_id;
            std::println(
                "{} {}:{} -> {}:{} ...",
                is_connect ? "connect" : "disconnect",
                op.talker.name,
                op.talker.unique_id,
                op.listener.name,
                op.listener.unique_id);

            // Leg matching: the relayed command, talker response and listener
            // response all carry OUR listener id+uid (the talker fields aren't
            // faithfully echoed by every device, so we key on the listener sink).
            uint8_t const relay_mt = is_connect ? ACMP_MESSAGE_TYPE_CONNECT_TX_COMMAND : ACMP_MESSAGE_TYPE_DISCONNECT_TX_COMMAND;
            uint8_t const tresp_mt = is_connect ? ACMP_MESSAGE_TYPE_CONNECT_TX_RESPONSE : ACMP_MESSAGE_TYPE_DISCONNECT_TX_RESPONSE;
            uint8_t const lresp_mt = is_connect ? ACMP_MESSAGE_TYPE_CONNECT_RX_RESPONSE : ACMP_MESSAGE_TYPE_DISCONNECT_RX_RESPONSE;

            HandshakeLegs legs{};
            ctrl.set_acmp_trace(trace);

            bool done = false;
            // Match on the LISTENER sink we asked to (dis)connect — the response's
            // talker fields aren't faithfully echoed by every device (e.g. the DSP processor),
            // so requiring talker_entity_id equality dropped real successes.
            auto const on_event = [&](auto const& ev) {
                if (is_connect) {
                    if (auto const* c = std::get_if<ConnectionAddedEvent>(&ev)) {
                        if (c->connection.listener_entity_id == *lid && c->connection.listener_unique_id == op.listener.unique_id) {
                            done = true;
                        }
                    }
                } else if (auto const* r = std::get_if<ConnectionRemovedEvent>(&ev)) {
                    if (r->listener_entity_id == *lid && r->listener_unique_id == op.listener.unique_id) {
                        done = true;
                    }
                }
                if (trace) {
                    if (auto const* t = std::get_if<AcmpTraceEvent>(&ev)) {
                        if (t->listener_entity_id == *lid && t->listener_unique_id == op.listener.unique_id) {
                            if (t->message_type == relay_mt) {
                                legs.relay_seen = true;
                            } else if (t->message_type == tresp_mt) {
                                legs.talker_status = t->status;
                            } else if (t->message_type == lresp_mt) {
                                legs.listener_status = t->status;
                            }
                        }
                    }
                }
            };
            // Some listeners (notably the DSP processor) only respond on a later attempt, so
            // re-issue the command up to a few times across the budget. Re-sending
            // a CONNECT/DISCONNECT for the same stream is idempotent.
            constexpr int kAttempts = 4;
            for (int attempt = 0; attempt < kAttempts && !done; ++attempt) {
                ctrl.dispatch(action, elapsed_ns());
                (void)pump(reactor, ctrl, 2500, [&] { return done; }, on_event);
            }
            ctrl.set_acmp_trace(false);
            std::println("  {}", done ? "OK" : "FAILED (no response after retries)");
            if (trace) {
                print_trace_verdict(op, is_connect, legs, done);
            }
            return done;
        }
        case OpKind::SetClockSource:
        case OpKind::GetClockSource: {
            auto const eid = resolve_or_report(op.entity, snap);
            if (!eid) {
                return false;
            }
            bool const is_set = (op.kind == OpKind::SetClockSource);
            ControllerAction action{};
            action.kind = is_set ? ControllerActionKind::SetClockSource : ControllerActionKind::GetClockSource;
            action.request.talker_entity_id = *eid;
            action.request.desc_index = static_cast<uint16_t>(op.clock_domain);
            action.request.clock_source_index = op.clock_source;
            std::println(
                "{} {} clock_domain={}{} ...",
                is_set ? "set-clock-source" : "get-clock-source",
                op.entity,
                op.clock_domain,
                is_set ? std::format(" clock_source={}", op.clock_source) : std::string{});
            ctrl.dispatch(action, elapsed_ns());
            bool done = false;
            bool ok = false;
            std::string text;
            (void)pump(
                reactor,
                ctrl,
                5000,
                [&] { return done; },
                [&](auto const& ev) {
                    if (auto const* s = std::get_if<StatusChangedEvent>(&ev)) {
                        text = s->status;
                        // Success if the AEM status name is SUCCESS (case-insensitive).
                        auto const lower = atdecc_tools::detail::to_lower(text);
                        ok = lower.find("success") != std::string::npos;
                        done = true;
                    }
                });
            std::println("  {}", done ? text : std::string{"FAILED (no response within 5s)"});
            return done && ok;
        }
    }
    return false;
}

// supervise: read one frame-movement counter off OUR local end of a leg.
// Sends GET_COUNTERS for (target, lc) and pumps the reactor until the matching
// CountersReadyEvent arrives (or the budget elapses). Returns the counter value
// (or nullopt when the counter could not be read / isn't valid) — nullopt means
// "unknown, leave as-is" to the caller.
auto sample_counter(MessageReactor& reactor, ControllerSimple& ctrl, Eui64 const& target, atdecc_tools::LocalCounter const& lc)
    -> std::optional<uint64_t>
{
    using atdecc_tools::ControllerAction;
    using atdecc_tools::ControllerActionKind;
    using atdecc_tools::CountersReadyEvent;

    ControllerAction action{};
    action.kind = ControllerActionKind::GetCounters;
    action.request.talker_entity_id = target;
    action.request.desc_type = lc.descriptor_type;
    action.request.desc_index = lc.descriptor_index;

    std::optional<uint64_t> result;
    bool done = false;
    auto const on_event = [&](auto const& ev) {
        if (auto const* c = std::get_if<CountersReadyEvent>(&ev)) {
            if (c->entity_id == target && c->descriptor_type == lc.descriptor_type && c->descriptor_index == lc.descriptor_index) {
                if (((c->counters_valid >> lc.counter_bit) & 1U) != 0U) {
                    result = c->counters[lc.counter_bit];
                }
                done = true;
            }
        }
    };
    // A single GET_COUNTERS datagram can be lost; re-issue across the budget.
    constexpr int kAttempts = 3;
    for (int attempt = 0; attempt < kAttempts && !done; ++attempt) {
        ctrl.dispatch(action, elapsed_ns());
        (void)pump(reactor, ctrl, 600, [&] { return done; }, on_event);
    }
    return result;
}

// supervise: do a clean teardown + reconnect of one stale leg.
//   DISCONNECT_RX -> 100ms -> DISCONNECT_TX -> 100ms -> CONNECT_RX
// The reactor is pumped between each step so the ACMP responses flow.
void clean_reset_leg(
    MessageReactor& reactor,
    ControllerSimple& ctrl,
    Eui64 const& talker_eid,
    uint16_t talker_uid,
    Eui64 const& listener_eid,
    uint16_t listener_uid)
{
    using atdecc_tools::ControllerAction;
    using atdecc_tools::ControllerActionKind;

    auto const make = [&](ControllerActionKind kind) {
        ControllerAction a{};
        a.kind = kind;
        a.request.talker_entity_id = talker_eid;
        a.request.talker_unique_id = talker_uid;
        a.request.listener_entity_id = listener_eid;
        a.request.listener_unique_id = listener_uid;
        return a;
    };
    auto const settle = [&] {
        (void)pump(reactor, ctrl, 200, [] { return false; }, [](auto const&) {});
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        (void)pump(reactor, ctrl, 50, [] { return false; }, [](auto const&) {});
    };

    ctrl.dispatch(make(ControllerActionKind::DisconnectStream), elapsed_ns());  // DISCONNECT_RX
    settle();
    ctrl.dispatch(make(ControllerActionKind::DisconnectTxStream), elapsed_ns());  // DISCONNECT_TX
    settle();
    ctrl.dispatch(make(ControllerActionKind::ConnectStream), elapsed_ns());  // CONNECT_RX
    (void)pump(reactor, ctrl, 500, [] { return false; }, [](auto const&) {});
}

}  // namespace

int main(int argc, char* argv[])
{
    using namespace statusbar;

    Config config;
    auto specs = build_arg_specs(config);
    auto cli_result = config::parse_cli_args(argc, argv, specs, print_usage, "statusbar-atdecc-ctl");
    if (!cli_result) {
        return config::handled_builtin_command(cli_result) ? 0 : 1;
    }
    if (config.interface_name.empty()) {
        std::println(stderr, "Error: --interface is required");
        return 1;
    }

    // Controller entity ID derived from our own NIC MAC (tool-id discriminated).
    atdecc_tools::CommonConfig cc;
    cc.interface_name = config.interface_name;
    auto id_result = atdecc_tools::resolve_controller_id(cc, atdecc_tools::TOOL_ID_CONTROLLER);
    if (!id_result) {
        std::println(stderr, "Error: cannot open interface '{}': {}", config.interface_name, id_result.error().message());
        return 1;
    }

    RawnetContext rawnet;
    if (!rawnet.open(config.interface_name, avtp::AVTP_ETHERTYPE, &ATDECC_MULTICAST_MAC)) {
        std::println(stderr, "Error: failed to open raw socket on '{}' (root/cap_net_raw required)", config.interface_name);
        return 1;
    }

    auto pollable = std::make_unique<ControllerSimple>(std::move(rawnet), *id_result);
    auto* ctrl = pollable.get();

    auto& stop = statusbar::itc::install_stop_signal();
    MessageReactor reactor{stop, elapsed_ns, 10};
    reactor.add(std::move(pollable));

    // DISCOVER phase: announce + read every entity's ENTITY descriptor (the
    // controller auto-enqueues that on ENTITY_AVAILABLE, populating names).
    atdecc_tools::ControllerAction discover{};
    discover.kind = atdecc_tools::ControllerActionKind::DiscoverAll;
    ctrl->dispatch(discover, elapsed_ns());
    (void)pump(reactor, *ctrl, config.discover_ms, [] { return false; }, [](auto const&) {});

    auto const snap = ctrl->get_display_entities();

    // ----- list -----
    if (config.command == "list") {
        if (!config.entity.empty()) {
            auto const eid = resolve_or_report(config.entity, snap);
            if (!eid) {
                return 1;
            }
            atdecc_tools::ControllerAction read{};
            read.kind = atdecc_tools::ControllerActionKind::ReadEntityDescriptors;
            read.request.talker_entity_id = *eid;
            ctrl->dispatch(read, elapsed_ns());
            bool got = false;
            (void)pump(
                reactor,
                *ctrl,
                5000,
                [&] { return got; },
                [&](auto const& ev) {
                    if (auto const* d = std::get_if<atdecc_tools::EntityDetailReadyEvent>(&ev)) {
                        if (d->detail.entity_id == *eid) {
                            std::println("== {} ({}) ==", d->detail.name, ieee::to_string(*eid).view());
                            for (auto const& line : d->detail.lines) {
                                std::println("{}", line.text);
                            }
                            got = true;
                        }
                    }
                });
            if (!got) {
                std::println(stderr, "Error: no descriptor detail for '{}' within 5s", config.entity);
                return 1;
            }
            return 0;
        }
        std::println("{} entit{} discovered:", snap.size(), snap.size() == 1 ? "y" : "ies");
        std::println("{:<28} {:<26} talker  listener", "NAME", "ENTITY_ID");
        for (auto const& e : snap) {
            std::println(
                "{:<28} {:<26} {:>6}  {:>8}",
                e.name.empty() ? "(unnamed)" : e.name,
                ieee::to_string(e.entity_id).view(),
                e.talker_stream_sources,
                e.listener_stream_sinks);
        }
        return 0;
    }

    // ----- validate (AEM model compliance) -----
    if (config.command == "validate") {
        if (config.entity.empty()) {
            std::println(stderr, "Error: --entity=<name> is required for validate");
            return 1;
        }
        auto const eid = resolve_or_report(config.entity, snap);
        if (!eid) {
            return 1;
        }
        // Crawl the full descriptor tree, then validate the cached raw descriptors.
        atdecc_tools::ControllerAction read{};
        read.kind = atdecc_tools::ControllerActionKind::ReadEntityDescriptors;
        read.request.talker_entity_id = *eid;
        ctrl->dispatch(read, elapsed_ns());
        bool crawled = false;
        (void)pump(
            reactor,
            *ctrl,
            8000,
            [&] { return crawled; },
            [&](auto const& ev) {
                if (auto const* d = std::get_if<atdecc_tools::EntityDetailReadyEvent>(&ev)) {
                    if (d->detail.entity_id == *eid) {
                        crawled = true;
                    }
                }
            });
        auto const descs = ctrl->cached_descriptors(*eid);
        if (descs.empty()) {
            std::println(stderr, "Error: no descriptors read from '{}' (entity unreachable?)", config.entity);
            return 1;
        }
        std::println("== AEM model validation: {} ({}) ==", config.entity, ieee::to_string(*eid).view());
        std::println("crawled {} descriptors{}", descs.size(), crawled ? "" : " (crawl did not signal complete; partial)");
        auto const findings = atdecc_tools::validate_aem_model(descs);
        int errors = 0;
        int warns = 0;
        for (auto const& f : findings) {
            if (f.severity == atdecc_tools::Severity::Error) {
                ++errors;
            } else if (f.severity == atdecc_tools::Severity::Warn) {
                ++warns;
            }
            std::println("  [{}] {}: {}", atdecc_tools::severity_name(f.severity), f.where, f.message);
        }
        std::println(
            "result: {} error(s), {} warning(s), {} info", errors, warns, static_cast<int>(findings.size()) - errors - warns);
        return errors == 0 ? 0 : 1;
    }

    // ----- get-rx-state (ACMP listener-sink connection state) -----
    // Diagnostic for the macOS acquire path: Audio MIDI Setup / avbdiagnose send
    // ACMP GET_RX_STATE to each of the entity's listener sinks and abort the
    // acquire ("Input stream N ... timed out") if any sink does not reply. This
    // probes exactly that: query one sink (--listener NAME:UID) or every sink of
    // an entity (--entity NAME), and report reply-vs-timeout per sink.
    if (config.command == "get-rx-state") {
        struct Sink
        {
            Eui64 id;
            uint16_t uid;
            std::string label;
        };
        std::vector<Sink> sinks;
        if (auto const l = atdecc_tools::parse_endpoint(config.listener)) {
            auto const lid = resolve_or_report(l->name, snap);
            if (!lid) {
                return 1;
            }
            sinks.push_back({*lid, l->unique_id, std::format("{}:{}", l->name, l->unique_id)});
        } else if (!config.entity.empty()) {
            auto const eid = resolve_or_report(config.entity, snap);
            if (!eid) {
                return 1;
            }
            uint16_t count = 0;
            for (auto const& e : snap) {
                if (e.entity_id == *eid) {
                    count = e.listener_stream_sinks;
                }
            }
            if (count == 0) {
                std::println(stderr, "Error: '{}' advertises no listener sinks", config.entity);
                return 1;
            }
            for (uint16_t i = 0; i < count; ++i) {
                sinks.push_back({*eid, i, std::format("{}:{}", config.entity, i)});
            }
        } else {
            std::println(stderr, "Error: get-rx-state needs --listener=NAME:UID or --entity=NAME");
            return 1;
        }

        int failures = 0;
        for (auto const& s : sinks) {
            bool got = false;
            std::string text;
            // Re-issue across the budget; a single ACMP datagram can be lost.
            constexpr int kAttempts = 3;
            for (int attempt = 0; attempt < kAttempts && !got; ++attempt) {
                ctrl->query_rx_state(s.id, s.uid, elapsed_ns());
                (void)pump(
                    reactor,
                    *ctrl,
                    600,
                    [&] { return got; },
                    [&](auto const& ev) {
                        if (auto const* r = std::get_if<atdecc_tools::RxStateEvent>(&ev)) {
                            if (r->listener_entity_id == s.id && r->listener_unique_id == s.uid) {
                                got = true;
                                if (r->connected) {
                                    text = std::format(
                                        "CONNECTED to talker {}:{} (status={})",
                                        ieee::to_string(r->talker_entity_id).view(),
                                        r->talker_unique_id,
                                        static_cast<int>(r->status));
                                } else {
                                    text = std::format("not connected (status={})", static_cast<int>(r->status));
                                }
                            }
                        }
                    });
            }
            if (got) {
                std::println("  {:<22} REPLY: {}", s.label, text);
            } else {
                std::println("  {:<22} TIMEOUT (no GET_RX_STATE_RESPONSE)", s.label);
                ++failures;
            }
        }
        std::println("get-rx-state: {} replied, {} timed out", sinks.size() - static_cast<size_t>(failures), failures);
        return failures == 0 ? 0 : 1;
    }

    // ----- get-tx-state (ACMP talker-source connection state) -----
    // Companion to get-rx-state: probes the TALKER side, which macOS reads
    // successfully. Run both to isolate whether silence is listener-specific.
    if (config.command == "get-tx-state") {
        auto const t = atdecc_tools::parse_endpoint(config.talker);
        if (!t) {
            std::println(stderr, "Error: get-tx-state needs --talker=NAME:UID");
            return 1;
        }
        auto const tid = resolve_or_report(t->name, snap);
        if (!tid) {
            return 1;
        }
        bool got = false;
        std::string text;
        constexpr int kAttempts = 3;
        for (int attempt = 0; attempt < kAttempts && !got; ++attempt) {
            ctrl->query_tx_state(*tid, t->unique_id, elapsed_ns());
            (void)pump(
                reactor,
                *ctrl,
                600,
                [&] { return got; },
                [&](auto const& ev) {
                    if (auto const* c = std::get_if<atdecc_tools::ConnectionAddedEvent>(&ev)) {
                        if (c->connection.talker_entity_id == *tid && c->connection.talker_unique_id == t->unique_id) {
                            got = true;
                            text = std::format(
                                "connection present (listener {})", ieee::to_string(c->connection.listener_entity_id).view());
                        }
                    }
                });
        }
        // The ACMP_TRACE env var prints every received ACMP frame, so a
        // GET_TX_STATE_RESPONSE shows there even when there is no connection
        // (which would not raise a ConnectionAddedEvent).
        std::println(
            "  {}:{:<20} {}", t->name, t->unique_id, got ? text : "no ConnectionAddedEvent (check ACMP_TRACE for mt=5 reply)");
        return 0;
    }

    // ----- single-op commands -----
    if (config.command == "connect" || config.command == "disconnect") {
        auto const t = atdecc_tools::parse_endpoint(config.talker);
        auto const l = atdecc_tools::parse_endpoint(config.listener);
        if (!t || !l) {
            std::println(stderr, "Error: --talker and --listener (NAME:UID) are required");
            return 1;
        }
        Op op{
            .kind = (config.command == "connect") ? OpKind::Connect : OpKind::Disconnect,
            .talker = *t,
            .listener = *l,
            .entity = {},
            .clock_domain = 0,
            .clock_source = 0};
        return execute_op(reactor, *ctrl, op, config.trace) ? 0 : 1;
    }
    if (config.command == "set-clock-source" || config.command == "get-clock-source") {
        if (config.entity.empty()) {
            std::println(stderr, "Error: --entity is required");
            return 1;
        }
        Op op{
            .kind = (config.command == "set-clock-source") ? OpKind::SetClockSource : OpKind::GetClockSource,
            .talker = {},
            .listener = {},
            .entity = config.entity,
            .clock_domain = static_cast<uint16_t>(config.clock_domain),
            .clock_source = static_cast<uint16_t>(config.clock_source)};
        return execute_op(reactor, *ctrl, op) ? 0 : 1;
    }

    // ----- batch -----
    if (config.command == "batch") {
        if (config.file.empty()) {
            std::println(stderr, "Error: --file is required for batch");
            return 1;
        }
        auto parsed = toml::parse_file(config.file);
        if (!parsed) {
            std::println(stderr, "Error: cannot parse '{}': {}", config.file, parsed.error().message());
            return 1;
        }
        std::string err;
        auto ops = atdecc_tools::parse_batch_ops(*parsed, err);
        if (!ops) {
            std::println(stderr, "Error: {}", err);
            return 1;
        }
        std::println("batch: {} operation(s)", ops->size());
        int failures = 0;
        for (auto const& op : *ops) {
            if (!execute_op(reactor, *ctrl, op)) {
                ++failures;
            }
        }
        std::println("batch done: {} ok, {} failed", ops->size() - static_cast<size_t>(failures), failures);
        return failures == 0 ? 0 : 1;
    }

    // ----- supervise (self-heal stale streams) -----
    // For each Connect op in the TOML, find whichever end is US (our entity's
    // modified-EUI-64), sample its frame-movement counter twice ~interval-ms
    // apart, and if it did not advance, do a clean teardown+reconnect:
    //   DISCONNECT_RX -> 100ms -> DISCONNECT_TX -> 100ms -> CONNECT_RX.
    // Legs neither of whose ends is us, and counters we can't read, are left
    // untouched. With --watch-ms>0 the whole pass repeats forever.
    if (config.command == "supervise") {
        if (config.file.empty()) {
            std::println(stderr, "Error: --file is required for supervise");
            return 1;
        }
        // Our audio entity's id is the NIC MAC as a modified EUI-64 -- NOT the
        // controller id (which carries a tool-id byte). That is the id a leg's
        // local end will match.
        auto const our_mac = net::read_interface_mac(config.interface_name);
        if (!our_mac) {
            std::println(stderr, "Error: cannot read MAC of interface '{}'", config.interface_name);
            return 1;
        }
        auto const our_eid = our_mac->to_modified_eui64();
        std::println("supervise: our entity id = {}", ieee::to_string(our_eid).view());

        auto parsed = toml::parse_file(config.file);
        if (!parsed) {
            std::println(stderr, "Error: cannot parse '{}': {}", config.file, parsed.error().message());
            return 1;
        }
        std::string err;
        auto ops = atdecc_tools::parse_batch_ops(*parsed, err);
        if (!ops) {
            std::println(stderr, "Error: {}", err);
            return 1;
        }

        auto run_pass = [&]() {
            auto const pass_snap = ctrl->get_display_entities();
            int legs = 0;
            int reset = 0;
            for (auto const& op : *ops) {
                if (op.kind != OpKind::Connect) {
                    continue;  // only stream connections are monitored
                }
                auto const tid = resolve_or_report(op.talker.name, pass_snap);
                auto const lid = resolve_or_report(op.listener.name, pass_snap);
                if (!tid || !lid) {
                    continue;  // unresolved name -> can't measure
                }
                auto const lc = atdecc_tools::select_local_counter(our_eid, *tid, op.talker.unique_id, *lid, op.listener.unique_id);
                if (!lc) {
                    continue;  // neither end is us -> skip
                }
                ++legs;
                auto const label =
                    std::format("{}:{} -> {}:{}", op.talker.name, op.talker.unique_id, op.listener.name, op.listener.unique_id);
                bool const we_are_listener = (lc->descriptor_type == atdecc::aem::DESCRIPTOR_STREAM_INPUT);
                char const* counter_name = we_are_listener ? "FRAMES_RX" : "FRAMES_TX";

                auto const s1 =
                    sample_counter(reactor, *ctrl, lc->descriptor_type == atdecc::aem::DESCRIPTOR_STREAM_INPUT ? *lid : *tid, *lc);
                if (!s1) {
                    std::println("supervise: {} UNKNOWN ({} unreadable) -> leave as-is", label, counter_name);
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(config.interval_ms));
                auto const s2 =
                    sample_counter(reactor, *ctrl, lc->descriptor_type == atdecc::aem::DESCRIPTOR_STREAM_INPUT ? *lid : *tid, *lc);
                if (!s2) {
                    std::println("supervise: {} UNKNOWN ({} unreadable on 2nd sample) -> leave as-is", label, counter_name);
                    continue;
                }
                if (atdecc_tools::is_stale(*s1, *s2)) {
                    std::println("supervise: {} STALE ({} {}) -> clean reset", label, counter_name, *s2);
                    clean_reset_leg(reactor, *ctrl, *tid, op.talker.unique_id, *lid, op.listener.unique_id);
                    ++reset;
                } else {
                    std::println("supervise: {} LIVE ({} {} -> {})", label, counter_name, *s1, *s2);
                }
            }
            std::println("supervise: pass done -- {} leg(s) measured, {} reset", legs, reset);
        };

        if (config.watch_ms > 0) {
            while (!stop.stop_requested()) {
                run_pass();
                // Re-discover between passes so newly-(re)appeared entities resolve.
                (void)pump(reactor, *ctrl, config.watch_ms, [&] { return stop.stop_requested(); }, [](auto const&) {});
                ctrl->dispatch(discover, elapsed_ns());
                (void)pump(reactor, *ctrl, config.discover_ms, [] { return false; }, [](auto const&) {});
            }
            return 0;
        }
        run_pass();
        return 0;
    }

    std::println(stderr, "Error: unknown command '{}'", config.command);
    return 1;
}
