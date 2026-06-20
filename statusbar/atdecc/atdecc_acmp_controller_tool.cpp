// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// ACMP Controller Tool - Simple command-line ACMP controller
// Uses IEEE 1722.1-2021 ACMP state machine to connect/disconnect streams

#include "statusbar/atdecc/atdecc_format.hpp"
#include "statusbar/avtp/avtp.hpp"
#include "statusbar/buffer/buffer.hpp"
#include "statusbar/config/config.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/itc/itc_stop_token.hpp"
#include "statusbar/net/net_message_reactor.hpp"
#include "statusbar/net/net_rawnet.hpp"
#include "statusbar/sm/sm.hpp"
#include "statusbar/status/status.hpp"
#include "statusbar/status/throw_or_abort.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <functional>
#include <memory>
#include <print>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using namespace statusbar;
using namespace statusbar::atdecc;
using namespace statusbar::avtp;
using namespace statusbar::net;
using namespace statusbar::ieee;
using namespace statusbar::sm;

// J. D. Koftinoff Software, Ltd. OUI-36 based EUI-64: 70:B3:D5:ED:C0:00:00:00
constexpr Eui64 MY_CONTROLLER_ENTITY_ID(0x70, 0xB3, 0xD5, 0xED, 0xC0, 0x00, 0x00, 0x00);

/// Configuration for ACMP controller
struct Config
{
    std::string interface_name;
    bool do_connect{true};
    // Full ACMP message type to send. RX-side (CONNECT_RX/DISCONNECT_RX) goes to
    // the listener (normal path); TX-side (CONNECT_TX/DISCONNECT_TX) goes DIRECTLY
    // to the talker's ACMP SM -- used to force-reset a wedged talker (e.g. a the DSP processor
    // that stopped advertising its stream after the listener restarted).
    uint8_t message_type{ACMP_MESSAGE_TYPE_CONNECT_RX_COMMAND};
    Eui64 talker_entity_id{};
    uint16_t talker_uid{0};
    Eui64 listener_entity_id{};
    uint16_t listener_uid{0};
};

// Logging observer for state machine transitions
struct LoggingObserver
{
    using Machine = AcmpControllerStateMachine<NullObserver>;

    void operator()(ControllerState from, ControllerEvent event, std::string_view action_name, ControllerState to) const
    {
        std::print(
            "[SM] {} --[{}]--> {} (action: {})\n",
            Machine::state_name(from),
            Machine::event_name(event),
            Machine::state_name(to),
            action_name);
    }
};

// ACMP Controller handler - integrates state machine with Pollable for MessageReactor
class AcmpControllerHandler : public Pollable
{
  public:
    AcmpControllerHandler(
        itc::StopToken& stop,
        int& exit_code_out,
        std::string_view interface_name,
        bool do_connect,
        uint8_t message_type,
        Eui64 talker_id,
        uint16_t talker_uid,
        Eui64 listener_id,
        uint16_t listener_uid)
        : stop_{stop}
        , exit_code_{exit_code_out}
        , do_connect_{do_connect}
        , message_type_{message_type}
        , talker_id_{talker_id}
        , talker_uid_{talker_uid}
        , listener_id_{listener_id}
        , listener_uid_{listener_uid}
    {
        (void)context_.open(interface_name, AVTP_ETHERTYPE, &ATDECC_MULTICAST_MAC);

        // Initialize context
        ctx_.my_id = MY_CONTROLLER_ENTITY_ID;

        // Set up transmit callback
        ctx_.tx_command = [this](AcmpCommandResponse const& cmd) -> bool { return send_command(cmd); };

        // Set up response callback
        ctx_.process_response = [this](AcmpCommandResponse const& resp) { process_response(resp); };

        std::print("[CTRL] Controller initialized with entity ID: ");
        std::string id_str;
        format_to(std::back_inserter(id_str), ctx_.my_id);
        std::print("{}\n", id_str);

        // Send the initial command
        send_initial_command();
    }

    [[nodiscard]] auto fd() const noexcept -> int override { return context_.fd(); }

    void on_ready(int64_t /*now_ns*/) override
    {
        Eui48 src_mac{};
        Eui48 dest_mac{};
        while (true) {
            auto result = context_.recv(&src_mac, &dest_mac, payload_buf_);
            if (!result || *result <= 0) {
                break;
            }
            auto const len = static_cast<size_t>(*result);
            dispatch_frame({payload_buf_.data(), len});
        }
    }

    void tick(int64_t now_ns) override
    {
        if (start_ns_ == 0) {
            start_ns_ = now_ns;
        }
        // Convert to sm::TimePoint for the state machine
        auto const tp = sm::TimePoint{std::chrono::nanoseconds{now_ns}};

        // Check for timeouts (the SM retries a few times, then gives up)
        if (controller_has_timeout(ctx_, tp)) {
            std::print("[CTRL] Timeout detected for inflight command\n");
            ctx_.current_inflight_index = ctx_.find_timed_out(tp);
            sm_.handle_event(ctx_, ControllerEvent::Timeout, tp);
        }

        // Overall deadline: exit if the talker/listener never responded, so the
        // tool never hangs waiting for a response (or for Ctrl-C) that won't come.
        constexpr int64_t OVERALL_TIMEOUT_NS = 5'000'000'000;
        if (!done_ && (now_ns - start_ns_) > OVERALL_TIMEOUT_NS) {
            std::print("[CTRL] No response within 5s; giving up.\n");
            finish(2);
        }
    }

    [[nodiscard]] auto finished() const noexcept -> bool override { return done_; }

    [[nodiscard]] auto valid() const noexcept -> bool { return context_.fd() >= 0; }

    [[nodiscard]] auto my_mac() const noexcept -> Eui48 const& { return context_.my_mac(); }

  private:
    void dispatch_frame(std::span<uint8_t const> payload)
    {
        // ACMP exists in two wire lengths and we must accept BOTH: the IEEE
        // 1722.1-2013 short form (control_data_length=44, 56 bytes total) and
        // the 2021 extended form (control_data_length=84, 96 bytes). Real
        // devices (e.g. third-party devices) reply with the 56-byte short form, so a
        // receiver that demands the 96-byte length silently drops every real
        // response.
        if (payload.size() < AcmpDu::LENGTH) {
            return;  // Too short even for the 2013 short form
        }

        // Check subtype
        auto subtype = payload[0];
        if (subtype != avtp::AvtpSubtype::acmp) {
            return;  // Not ACMP
        }

        // Parse the ACMP PDU into the extended in-memory form the SM uses.
        // The two wire forms share an identical first 56 bytes, so a short-form
        // frame loads into AcmpDu and is zero-extended into AcmpCommandResponse.
        AcmpCommandResponse resp{};
        if (payload.size() >= AcmpDu2021::LENGTH) {
            (void)protocol::load_unchecked(payload, &resp);
        } else {
            AcmpDu short_pdu{};
            (void)protocol::load_unchecked(payload.subspan(0, AcmpDu::LENGTH), &short_pdu);
            resp = acmp_command_response_from_pdu(short_pdu);
        }

        std::print("[RX] ");
        print_acmp(resp);

        // Check if this response matches an inflight command
        if (controller_should_handle_response(ctx_, resp)) {
            ctx_.rcvd_cmd_resp = resp;
            ctx_.current_inflight_index = ctx_.find_inflight(resp);
            sm_.handle_event(ctx_, ControllerEvent::RcvdResponse);
        }
    }

    void send_initial_command()
    {
        // Fill command parameters
        ctx_.command_params.message_type = message_type_;
        std::print("[CTRL] Sending {}\n", acmp_message_type_name(message_type_));

        ctx_.command_params.talker_entity_id = talker_id_;
        ctx_.command_params.talker_unique_id = talker_uid_;
        ctx_.command_params.listener_entity_id = listener_id_;
        ctx_.command_params.listener_unique_id = listener_uid_;
        ctx_.command_params.flags = 0;
        ctx_.command_params.connection_count = 0;
        ctx_.command_params.stream_vlan_id = 0;

        // Trigger the state machine
        sm_.handle_event(ctx_, ControllerEvent::DoCommand);
    }

    auto send_command(AcmpCommandResponse const& cmd) -> bool
    {
        std::print("[TX] ");
        print_acmp(cmd);

        // Serialize for L2: the 56-byte (control_data_length=44) short form, which
        // real devices (third-party devices) require -- the 96-byte extended PDU is for
        // UDP-encapsulated ACMP only and is silently dropped on L2 by such devices.
        std::array<uint8_t, 96> buffer{};  // Room for extended ACMP (UDP case)
        auto wire = acmp_serialize_2016(cmd, buffer);

        // Send to multicast
        auto result = context_.send(&ATDECC_MULTICAST_MAC, wire);
        if (!result) {
            std::print("[TX] Failed to send\n");
            return false;
        }

        return true;
    }

    void process_response(AcmpCommandResponse const& resp)
    {
        std::print("[CTRL] Response received: status={} ({})\n", resp.status(), acmp_status_name(resp.status()));

        if (resp.status() == ACMP_STATUS_SUCCESS) {
            std::print("[CTRL] SUCCESS - ");
            if (do_connect_) {
                std::print("Stream connected!\n");
            } else {
                std::print("Stream disconnected!\n");
            }

            // Print stream info
            std::print("[CTRL] Stream ID: ");
            std::string stream_str;
            format_to(std::back_inserter(stream_str), resp.stream_id);
            std::print("{}\n", stream_str);

            std::print("[CTRL] Dest MAC: ");
            std::string mac_str;
            format_to(std::back_inserter(mac_str), resp.stream_dest_mac);
            std::print("{}\n", mac_str);

            std::print("[CTRL] Connection count: {}\n", static_cast<uint16_t>(resp.connection_count));
        } else {
            std::print("[CTRL] FAILED - {}\n", acmp_status_name(resp.status()));
        }

        // The command is complete; exit instead of waiting for Ctrl-C, so a
        // one-shot CONNECT/DISCONNECT can't leave a lingering controller process.
        std::print("[CTRL] Command complete.\n");
        finish(resp.status() == ACMP_STATUS_SUCCESS ? 0 : 1);
    }

    void print_acmp(AcmpCommandResponse const& acmp)
    {
        std::print("ACMP {} ", acmp_message_type_name(acmp.message_type()));
        if (acmp.is_response()) {
            std::print("status={} ", acmp_status_name(acmp.status()));
        }
        std::print("seq={}\n", static_cast<uint16_t>(acmp.sequence_id));

        std::print("    controller=");
        std::string ctrl_str;
        format_to(std::back_inserter(ctrl_str), acmp.controller_entity_id);
        std::print("{}\n", ctrl_str);

        std::print("    talker=");
        std::string talker_str;
        format_to(std::back_inserter(talker_str), acmp.talker_entity_id);
        std::print("{} uid={}\n", talker_str, static_cast<uint16_t>(acmp.talker_unique_id));

        std::print("    listener=");
        std::string listener_str;
        format_to(std::back_inserter(listener_str), acmp.listener_entity_id);
        std::print("{} uid={}\n", listener_str, static_cast<uint16_t>(acmp.listener_unique_id));
    }

    // Mark the command complete and ask the reactor to stop, so a one-shot
    // CONNECT/DISCONNECT exits cleanly rather than lingering until Ctrl-C.
    void finish(int code)
    {
        if (!done_) {
            exit_code_ = code;
            done_ = true;
            stop_.request_stop();
        }
    }

    itc::StopToken& stop_;
    int& exit_code_;
    RawnetContext context_{};
    std::array<uint8_t, 2048> payload_buf_{};
    ControllerContext ctx_{16};
    AcmpControllerStateMachine<LoggingObserver> sm_{LoggingObserver{}};
    int64_t start_ns_{0};
    bool done_{false};
    bool do_connect_;
    uint8_t message_type_;
    Eui64 talker_id_;
    uint16_t talker_uid_;
    Eui64 listener_id_;
    uint16_t listener_uid_;
};

auto build_arg_specs(Config& config) -> statusbar::args::ArgumentSpecs
{
    statusbar::args::ArgumentSpecs specs;

    specs.add_device(
        "interface", "Network interface (e.g., en0, eth0)", "", [&](auto v) { config.interface_name = std::string{v}; });

    specs.add_choice(
        "action",
        "Action: CONNECT/DISCONNECT (RX, to listener) or CONNECT_TX/DISCONNECT_TX (direct to talker)",
        {"CONNECT", "DISCONNECT", "CONNECT_TX", "DISCONNECT_TX"},
        "CONNECT",
        [&](auto v) {
            if (v == "CONNECT" || v == "connect") {
                config.do_connect = true;
                config.message_type = ACMP_MESSAGE_TYPE_CONNECT_RX_COMMAND;
            } else if (v == "DISCONNECT" || v == "disconnect") {
                config.do_connect = false;
                config.message_type = ACMP_MESSAGE_TYPE_DISCONNECT_RX_COMMAND;
            } else if (v == "CONNECT_TX" || v == "connect-tx") {
                config.do_connect = true;
                config.message_type = ACMP_MESSAGE_TYPE_CONNECT_TX_COMMAND;
            } else if (v == "DISCONNECT_TX" || v == "disconnect-tx") {
                config.do_connect = false;
                config.message_type = ACMP_MESSAGE_TYPE_DISCONNECT_TX_COMMAND;
            } else {
                std::print(stderr, "Error: Invalid action '{}'. Use CONNECT/DISCONNECT/CONNECT_TX/DISCONNECT_TX.\n", v);
                statusbar::throw_or_abort(std::errc::invalid_argument);
            }
        });

    specs.add<Eui64>(
        "talker-entity-id", "Talker Entity ID (EUI-64)", config.talker_entity_id, [&](auto v) { config.talker_entity_id = v; });

    specs.add<int64_t>("talker-uid", "Talker Unique ID (0-65535)", 0, [&](auto v) {
        if (v < 0 || v > 65535) {
            std::print(stderr, "Error: Invalid talker unique ID '{}' (must be 0-65535)\n", v);
            statusbar::throw_or_abort(std::errc::invalid_argument);
        }
        config.talker_uid = static_cast<uint16_t>(v);
    });

    specs.add<Eui64>("listener-entity-id", "Listener Entity ID (EUI-64)", config.listener_entity_id, [&](auto v) {
        config.listener_entity_id = v;
    });

    specs.add<int64_t>("listener-uid", "Listener Unique ID (0-65535)", 0, [&](auto v) {
        if (v < 0 || v > 65535) {
            std::print(stderr, "Error: Invalid listener unique ID '{}' (must be 0-65535)\n", v);
            statusbar::throw_or_abort(std::errc::invalid_argument);
        }
        config.listener_uid = static_cast<uint16_t>(v);
    });

    return specs;
}

void print_usage(char const* prog, statusbar::args::ArgumentSpecs const& specs)
{
    std::print(stderr, "Usage: {} [options]\n", prog);
    std::print(stderr, "\nOptions:\n");

    std::string help;
    specs.format_help_to(std::back_inserter(help));
    std::print(stderr, "{}", help);

    std::print(stderr, "\nExamples:\n");
    std::print(stderr, "  {} --interface=eth0 --action=CONNECT \\\n", prog);
    std::print(stderr, "    --talker-entity-id=00:11:22:33:44:55:66:77 --talker-uid=0 \\\n");
    std::print(stderr, "    --listener-entity-id=aa:bb:cc:dd:ee:ff:00:11 --listener-uid=1\n");
    std::print(stderr, "\nConfiguration file example:\n");
    std::print(stderr, "  interface = \"eth0\"\n");
    std::print(stderr, "  action = \"CONNECT\"\n");
    std::print(stderr, "  talker-entity-id = \"00:11:22:33:44:55:66:77\"\n");
    std::print(stderr, "  talker-uid = 0\n");
    std::print(stderr, "  listener-entity-id = \"aa:bb:cc:dd:ee:ff:00:11\"\n");
    std::print(stderr, "  listener-uid = 1\n");
    std::print(stderr, "\nController Entity ID: 70:B3:D5:ED:C0:00:00:00\n");
    std::print(stderr, "                      (J. D. Koftinoff Software, Ltd. OUI-36)\n");
}

}  // namespace

auto main(int argc, char** argv) -> int
{
    // Build config and specs with bindings
    Config config;
    auto specs = build_arg_specs(config);

    // Parse CLI arguments, handle --completion, --help, --config-load, --config-save, apply bindings
    auto cli_result = statusbar::config::parse_cli_args(argc, argv, specs, print_usage, "statusbar-acmp-controller");
    if (!cli_result) {
        return statusbar::config::handled_builtin_command(cli_result) ? 0 : 1;
    }

    // Validate required options
    if (config.interface_name.empty()) {
        std::print(stderr, "Error: --interface is required\n\n");
        print_usage(argv[0], specs);
        return EXIT_FAILURE;
    }

    if (!config.talker_entity_id.is_set() || !config.listener_entity_id.is_set()) {
        std::print(stderr, "Error: --talker-entity-id and --listener-entity-id are required\n\n");
        print_usage(argv[0], specs);
        return EXIT_FAILURE;
    }

    std::print("ACMP Controller Tool\n");
    std::print("====================\n");
    std::print("Interface: {}\n", config.interface_name);
    std::print("Action: {}\n", config.do_connect ? "CONNECT" : "DISCONNECT");

    std::string talker_str, listener_str;
    format_to(std::back_inserter(talker_str), config.talker_entity_id);
    format_to(std::back_inserter(listener_str), config.listener_entity_id);
    std::print("Talker: {} uid={}\n", talker_str, config.talker_uid);
    std::print("Listener: {} uid={}\n", listener_str, config.listener_uid);
    std::print("\n");

    // Create handler. The reactor stops as soon as the command completes (or its
    // overall deadline elapses); exit_code is written by the handler before then.
    auto& stop = statusbar::itc::install_stop_signal();
    int exit_code = 0;
    auto handler_ptr = std::make_unique<AcmpControllerHandler>(
        stop,
        exit_code,
        config.interface_name,
        config.do_connect,
        config.message_type,
        config.talker_entity_id,
        config.talker_uid,
        config.listener_entity_id,
        config.listener_uid);

    if (!handler_ptr->valid()) {
        std::print(stderr, "Error: Failed to open raw socket on interface '{}'\n", config.interface_name);
        std::print(stderr, "       (May require root/sudo or appropriate capabilities)\n");
        return EXIT_FAILURE;
    }

    std::print("Listening on {} (MAC: ", config.interface_name);
    std::string mac_str;
    format_to(std::back_inserter(mac_str), handler_ptr->my_mac());
    std::print("{})\n\n", mac_str);

    // Create reactor and run (reactor takes ownership of handler)
    MessageReactor reactor{stop, monotonic_ns, 100};
    reactor.add(std::move(handler_ptr));

    reactor.run();

    return exit_code;
}