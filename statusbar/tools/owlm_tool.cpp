// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// owlm_tool — one-way latency measurement tool for Linux.
//
// Sends and receives small UDP datagrams between two end stations,
// stamps both directions in the gPTP clock domain, and tracks
// per-source latency statistics. See docs/OWLM.md.
//
// Requires CAP_NET_RAW + CAP_NET_ADMIN, or root.
//   sudo setcap cap_net_raw,cap_net_admin=ep owlm_tool

#if defined(__linux__)

#    include "statusbar/config/config.hpp"
#    include "statusbar/itc/itc_stop_token.hpp"
#    include "statusbar/net/net.hpp"
#    include "statusbar/net/net_posix_util.hpp"
#    include "statusbar/owlm/owlm.hpp"
#    include "statusbar/owlm/owlm_codec.hpp"
#    include "statusbar/owlm/owlm_tool_config.hpp"
#    include "statusbar/owlm/owlm_tx_identity.hpp"
#    include "statusbar/realtime/realtime_base.hpp"
#    include "statusbar/stun/stun.hpp"
#    include "statusbar/udptun/udptun.hpp"

#    include <atomic>
#    include <chrono>
#    include <cstdint>
#    include <cstdio>
#    include <fstream>
#    include <iostream>
#    include <iterator>
#    include <print>
#    include <span>
#    include <string>
#    include <thread>
#    include <utility>

using namespace statusbar;
using namespace statusbar::owlm::tool;

namespace {

void print_usage(char const* program_name, statusbar::args::ArgumentSpecs const& specs)
{
    std::println("Usage: {} [options]", program_name);
    std::println("\nOne-way latency measurement tool. Requires CAP_NET_RAW + CAP_NET_ADMIN.\n");
    std::println("Options:");
    std::string help;
    specs.format_help_to(std::back_inserter(help));
    std::print("{}", help);
    std::println("\nExamples:");
    std::println("  {} --gptp-interface=eth0 --owlm-interface=eth1 --peer=10.0.0.5 --tx-interval-us=1000", program_name);
    std::println("  {} --gptp-interface=eth0 --peer=239.1.2.3 --mcast-ttl=2 --dscp=46", program_name);
}

int run_reflect(Config const& cli)
{
    udptun::ReflectConfig const rcfg{
        .local_port = cli.local_port,
        .interface = cli.owlm_interface,
        .ipv6 = cli.ipv6,
        .dscp = cli.dscp,
        .label = "owlm_tool reflect",
    };
    return udptun::run_reflect(owlm::OwlmCodec{}, rcfg);
}

/// Run a STUN rendezvous and update `cli` so build_session_config picks
/// up the discovered peer address. The opened FileDescriptor is moved
/// into `socket_out` for the caller to stash into SessionConfig. On
/// failure returns false and leaves outputs untouched (logs reason to
/// stderr).
[[nodiscard]] auto perform_rendezvous_into(Config& cli, net::FileDescriptor& socket_out) -> bool
{
    statusbar::crypto::Aes128SivKey key{};
    if (!net::parse_hex_into(cli.rendezvous_key_hex, std::span<uint8_t>{key.data})) {
        std::println(stderr, "error: --rendezvous-key must be 64 hex chars");
        return false;
    }
    stun::SessionId session_id{};
    if (!net::parse_hex_into(cli.rendezvous_session_id_hex, std::span<uint8_t>{session_id.bytes})) {
        std::println(stderr, "error: --rendezvous-session-id must be 32 hex chars");
        return false;
    }
    std::string host{};
    std::string port{};
    if (!net::split_host_port(cli.rendezvous_server, host, port)) {
        std::println(stderr, "error: --rendezvous-server must be HOST:PORT");
        return false;
    }
    auto server_addr = net::SocketAddress::from_string(host, port, net::SocketDatagram);
    if (!server_addr) {
        std::println(stderr, "error: cannot resolve rendezvous server '{}:{}'", host, port);
        return false;
    }

    // owlm builds its real EUI-64 from the local MAC during Session
    // construction. For rendezvous purposes we synthesize a placeholder
    // EUI-64 from the role + session id; what matters to the server is
    // only that the two peers' EUI-64s differ.
    ieee::Eui64 placeholder_eui{};
    auto eui_span = placeholder_eui.span();
    for (size_t i = 0; i < eui_span.size(); ++i) {
        eui_span[i] = session_id.bytes[i];
    }
    eui_span[0] = (cli.rendezvous_role == "responder") ? 0x02U : 0x01U;

    stun::RendezvousConfig rcfg{
        .server_address = *server_addr,
        .session_id = session_id,
        .client_eui64 = placeholder_eui,
        .role = (cli.rendezvous_role == "responder") ? stun::Role::Responder : stun::Role::Initiator,
        .shared_key = key,
        .local_port = cli.local_port,
        .local_interface = cli.owlm_interface,
        .timeout_ms = cli.rendezvous_timeout_ms,
        .verbose = cli.rendezvous_verbose,
    };
    std::println(stderr, "[owlm] starting STUN rendezvous against {} as {}", server_addr->to_string(), cli.rendezvous_role);
    auto result = stun::perform_rendezvous(rcfg);
    if (!result) {
        std::println(stderr, "error: rendezvous failed: {}", result.error().message());
        return false;
    }
    std::string peer_host{};
    uint16_t peer_port = 0;
    if (!net::address_to_host_port(result->peer_reflexive_address, peer_host, peer_port)) {
        std::println(stderr, "error: rendezvous returned an unparsable peer address");
        return false;
    }
    std::println(
        stderr,
        "[owlm] rendezvous done. my reflexive={} peer={}:{}",
        result->my_reflexive_address.to_string(),
        peer_host,
        peer_port);
    cli.peer_host = std::move(peer_host);
    cli.peer_port = peer_port;
    socket_out = std::move(result->socket);
    return true;
}

/// Write one AtomicHistogram snapshot to a CSV. One row per bin:
/// bin_low_ns,bin_high_ns,count. The underflow row has an empty
/// bin_low_ns (no lower bound); the overflow row has an empty
/// bin_high_ns. Both ends are always emitted so the output schema is
/// stable across runs even when the tails are zero.
[[nodiscard]] auto write_histogram_csv(std::string const& path, stats::AtomicHistogram<128>::Snapshot const& hist) -> bool
{
    std::ofstream out{path};
    if (!out) {
        std::println(stderr, "warning: cannot open {} for writing", path);
        return false;
    }
    out << "bin_low_ns,bin_high_ns,count\n";
    out << ',' << hist.config.low_ns << ',' << hist.underflow << '\n';
    for (size_t i = 0; i < hist.num_bins; ++i) {
        out << hist.bin_low(i) << ',' << hist.bin_high(i) << ',' << hist.bins[i] << '\n';
    }
    out << hist.config.high_ns << ',' << ',' << hist.overflow << '\n';
    if (!out) {
        std::println(stderr, "warning: write to {} failed", path);
        return false;
    }
    return true;
}

int run_normal(Config const& cli_const)
{
    Config cli = cli_const;  // mutable copy: rendezvous fills in peer_host/peer_port
    net::FileDescriptor preopened_socket{};

    if (!cli.rendezvous_server.empty()) {
        if (!perform_rendezvous_into(cli, preopened_socket)) {
            return 1;
        }
    }

    if (cli.tx_interval_us > 0 && cli.peer_host.empty()) {
        std::println(stderr, "error: --peer or --rendezvous-server is required when --tx-interval-us > 0");
        return 1;
    }

    // lock_future=true: on low-RAM systems with active swap, any heap
    // allocation in the realtime path risks a multi-ms swap-in stall.
    // Lock all current AND future pages, accepting the per-allocation
    // lock cost.
    (void)realtime::print_realtime_diagnostics_and_prepare(cli.verbose, /*lock_future=*/true);

    if (cli.enable_tripwire) {
        // Trace must observe the same CPU the realtime timer thread runs on.
        cli.trace.cpu = cli.wan_timer.cpu;
    }

    udptun::SessionConfig scfg = build_session_config(cli);
    scfg.time_source = build_time_source_config(cli);
    scfg.preopened_udp = std::move(preopened_socket);

    owlm::TxIdentityParams const id_params{
        .redundant = cli.redundant,
        .legacy_mid = cli.id_mid,
        .temporal_shift_ms = cli.temporal_shift_ms,
    };

#    if __cpp_exceptions
    try {
#    endif
        udptun::Session<owlm::OwlmCodec> session{std::move(scfg), owlm::OwlmCodec{}, [&id_params](ieee::Eui48 const& mac) {
                                                     return owlm::build_tx_identity(mac, id_params);
                                                 }};
        udptun::print_eui64_banner("OWLM", session.tx_state(), cli.temporal_shift_ms);

        auto& stop = statusbar::itc::install_stop_signal();

        // Optional --duration-s watchdog: a side thread that raises the
        // same stop flag SIGINT raises once the wall-clock deadline is
        // reached. Uses stop.wait_for_stop() so the watchdog wakes
        // immediately when stop is requested from any other path.
        // No-op when duration_s <= 0.
        std::thread duration_watchdog;
        if (cli.duration_s > 0.0) {
            auto const dur_ns = static_cast<int64_t>(cli.duration_s * 1e9);
            duration_watchdog = std::thread{[&stop, dur_ns]() {
                auto const deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds{dur_ns};
                while (!stop.stop_requested()) {
                    if (std::chrono::steady_clock::now() >= deadline) {
                        std::println(stderr, "[owlm] --duration-s deadline reached; initiating graceful shutdown");
                        stop.request_stop();
                        return;
                    }
                    (void)stop.wait_for_stop(std::chrono::milliseconds(100));
                }
            }};
        }

        session.run(stop);

        // session.run() has returned. The watchdog will have exited
        // either because it called request_stop() itself (deadline
        // reached) or because stop was requested from another path
        // (signal handler, session.run's own exit) and the watchdog's
        // next wait_for_stop returned true.
        if (duration_watchdog.joinable()) {
            duration_watchdog.join();
        }

        session.drain_after_stop(session.natural_drain_ns());
        session.print_final_summary(std::cout, "OWLM");

        // Report overrun counter from the active bridge's
        // AtomicTripleBuffer. Non-zero is benign — it just means the
        // sampler (~500 Hz) published a new sample before the wan_timer
        // consumer (~8 kHz) got around to reading the previous one,
        // which is normal at these rates. There's no "did the race
        // fire" counter to track because the triple buffer makes that
        // race structurally impossible.
        if (auto const buf = session.bridge_buffer_stats(); buf.available) {
            std::println(std::cout, "[owlm] bridge buffer: overruns={}", buf.overruns);
        }

        // Dump the realtime-timer histograms if requested. Snapshot is
        // populated only on the realtime-timer code path (gPTP / ptp4l +
        // wan_timer.period_ns > 0); polling path leaves it empty.
        if (auto const& ws = session.latest_wake_stats(); ws.has_value()) {
            if (!cli.wan_timer_wake_stats_csv_path.empty()) {
                (void)write_histogram_csv(cli.wan_timer_wake_stats_csv_path, ws->error_histogram);
            }
            if (!cli.wan_timer_duration_stats_csv_path.empty()) {
                (void)write_histogram_csv(cli.wan_timer_duration_stats_csv_path, ws->duration_histogram);
            }
        } else if (!cli.wan_timer_wake_stats_csv_path.empty() || !cli.wan_timer_duration_stats_csv_path.empty()) {
            std::println(
                stderr,
                "warning: no wake-stats snapshot available — the realtime timer didn't run (gPTP not "
                "ready or --wan_timer.period_ns=0). Skipping histogram CSV output.");
        }

        if (auto const st = session.flush_csv(); !st) {
            std::println(stderr, "warning: CSV flush failed: {}", st.error().message());
        }
        if (auto const dropped = session.csv_records_dropped(); dropped > 0) {
            std::println(
                stderr,
                "warning: csv_records_dropped={} — the CSV writer thread fell behind disk; "
                "reduce --tx-interval-us or move --csv-output to faster storage",
                dropped);
        }
#    if __cpp_exceptions
    } catch (std::system_error const& e) {
        std::println(stderr, "error: {}", e.what());
        return 1;
    }
#    endif
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    Config cli;
    auto specs = build_arg_specs(cli);
    auto cli_result = statusbar::config::parse_cli_args(argc, argv, specs, print_usage, "owlm_tool");
    if (!cli_result) {
        return statusbar::config::handled_builtin_command(cli_result) ? 0 : 1;
    }
    // --packet-profile presets must be applied AFTER parsing: the arg framework
    // runs every unset option's default callback after the explicit ones, which
    // would clobber a callback that tried to set payload-bytes/tx-interval-us.
    if (cli.packet_profile_audio) {
        cli.payload_bytes = 1420;  // 1452-byte datagram - 32-byte OWLM header
        cli.tx_interval_us = 458;  // 44 frames @ 96 kHz
    }
    return (cli.mode == Mode::Reflect) ? run_reflect(cli) : run_normal(cli);
}

#else

#    include <print>

int main()
{
    std::println(stderr, "owlm_tool requires Linux.");
    return 1;
}

#endif
