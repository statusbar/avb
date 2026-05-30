// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// AVB Entity Stereo I/O Implementation
/// Implements the AvbEntityStereoIO class for stereo audio passthrough with DSP

#include "statusbar/avb_entity/avb_entity_stereo_io.hpp"

#include "statusbar/atdecc/atdecc.hpp"
#include "statusbar/atdecc/atdecc_adp.hpp"
#include "statusbar/atdecc/atdecc_aem_descriptor.hpp"
#include "statusbar/avtp/avtp.hpp"
#include "statusbar/buffer/buffer.hpp"
#include "statusbar/buffer/span_utils.hpp"
#include "statusbar/dsp/dsp.hpp"
#include "statusbar/dsp/dsp_biquad.hpp"
#include "statusbar/dsp/dsp_vec_base.hpp"
#include "statusbar/gptp/gptp.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/ieee/ieee_ethernet.hpp"
#include "statusbar/nanoavb/nanoavb.hpp"
#include "statusbar/nanoavb/nanoavb_acmp.hpp"
#include "statusbar/nanoavb/nanoavb_adp.hpp"
#include "statusbar/nanoavb/nanoavb_components.hpp"
#include "statusbar/nanoavb/nanoavb_entity.hpp"
#include "statusbar/nanoavb/nanoavb_entity_model.hpp"
#include "statusbar/nanoavb/nanoavb_gptp_sm.hpp"
#include "statusbar/nanoavb/nanoavb_listener_engine_sm.hpp"
#include "statusbar/nanoavb/nanoavb_msrp_listener_sm.hpp"
#include "statusbar/nanoavb/nanoavb_msrp_talker_sm.hpp"
#include "statusbar/nanoavb/nanoavb_mvrp_sm.hpp"
#include "statusbar/nanoavb/nanoavb_srp.hpp"
#include "statusbar/nanoavb/nanoavb_supervisor_sm.hpp"
#include "statusbar/nanoavb/nanoavb_talker_engine_sm.hpp"
#include "statusbar/net/net_message_reactor.hpp"
#include "statusbar/ptpclient/ptpclient.hpp"
#include "statusbar/realtime/realtime.hpp"
#include "statusbar/sm/sm.hpp"
#include "statusbar/status/status.hpp"
#include "statusbar/tsn/tsn.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <functional>
#include <memory>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace statusbar::avb_entity {

using namespace statusbar::atdecc;
using namespace statusbar::atdecc::aem;
using namespace statusbar::nanoavb;

//
// Constructor / Destructor
//

AvbEntityStereoIO::AvbEntityStereoIO(AvbEntityStereoIOConfig config)
    : config_{std::move(config)}
    , components_{create_components()}
{
    // Configure biquad filters for both channels
    configure_filter(config_.filter_freq_hz, config_.filter_gain_db, config_.filter_q);

    // Derive talker stream ID from entity ID (entity_id + unique_id 0x0000)
    talker_stream_id_ = config_.entity_id;
}

AvbEntityStereoIO::~AvbEntityStereoIO()
{
    try {
        if (running_) {
            (void)stop();
        }
    } catch (...) {  // NOLINT(bugprone-empty-catch) - destructors must not throw
    }
}

//
// Entity Model Creation
//

auto AvbEntityStereoIO::create_entity_model() const -> nanoavb::EntityModel
{
    using namespace statusbar::atdecc::aem;

    // Configure for minimal stereo I/O entity
    EntityModelConfig cfg;
    cfg.max_configurations = 1;
    cfg.max_stream_inputs = 1;
    cfg.max_stream_outputs = 1;
    cfg.max_avb_interfaces = 1;
    cfg.max_audio_units = 1;
    cfg.max_audio_clusters = 2;  // One for input, one for output

    EntityModel model{cfg};

    // Set up entity descriptor
    DescriptorEntity entity{};
    entity.entity_id = config_.entity_id;
    entity.entity_model_id = config_.entity_model_id;
    entity.entity_capabilities = atdecc::entity_capabilities::AEM_SUPPORTED | atdecc::entity_capabilities::CLASS_A_SUPPORTED |
        atdecc::entity_capabilities::GPTP_SUPPORTED;
    entity.talker_stream_sources.set(1);
    entity.talker_capabilities = atdecc::talker_capabilities::IMPLEMENTED | atdecc::talker_capabilities::AUDIO_SOURCE;
    entity.listener_stream_sinks.set(1);
    entity.listener_capabilities = atdecc::listener_capabilities::IMPLEMENTED | atdecc::listener_capabilities::AUDIO_SINK;
    entity.entity_name = AtdeccString{config_.entity_name.c_str()};
    entity.firmware_version = AtdeccString{config_.firmware_version.c_str()};
    entity.group_name = AtdeccString{"Stereo I/O"};
    entity.serial_number = AtdeccString{"000001"};
    entity.current_configuration.set(0);

    model.set_entity(entity);

    // Add configuration descriptor
    DescriptorConfiguration config_desc{};
    config_desc.object_name = AtdeccString{"Default"};
    config_desc.localized_description.set(0xFFFF);
    config_desc.descriptor_counts_count.set(0);
    (void)model.add_configuration(config_desc);

    // Add AVB interface descriptor
    DescriptorAvbInterface avb_if{};
    avb_if.object_name = AtdeccString{"AVB Port 1"};
    avb_if.localized_description.set(0xFFFF);
    // MAC address will be set at runtime from network interface
    (void)model.add_avb_interface(avb_if);

    // Stream format: AM824, 48 kHz, 2 channels (stereo)
    // Format is encoded in EUI-64: IEC 61883-6 AM824 format
    // See IEEE 1722.1-2021 Annex A.5
    ieee::Eui64 stream_format{};
    // IEC 61883-6 AM824 format indicator with sample rate and channel count
    // This is a simplified format encoding - proper implementation would use
    // the full format descriptor structure
    stream_format = ieee::Eui64{0x00, 0xA0, 0x02, 0x00, 0x02, 0x01, 0x00, 0x00};

    // Add stream input (listener) descriptor
    DescriptorStream stream_in{};
    stream_in.descriptor_type.set(DESCRIPTOR_STREAM_INPUT);
    stream_in.object_name = AtdeccString{"Stereo In"};
    stream_in.localized_description.set(0xFFFF);
    stream_in.clock_domain_index.set(0);
    stream_in.stream_flags.set(0);
    stream_in.current_format = stream_format;
    stream_in.avb_interface_index.set(0);
    stream_in.buffer_length.set(SAMPLES_PER_PACKET * CHANNELS * 4);  // 4 bytes per sample (AM824)
    (void)model.add_stream_input(stream_in);

    // Add stream output (talker) descriptor
    DescriptorStream stream_out{};
    stream_out.descriptor_type.set(DESCRIPTOR_STREAM_OUTPUT);
    stream_out.object_name = AtdeccString{"Stereo Out"};
    stream_out.localized_description.set(0xFFFF);
    stream_out.clock_domain_index.set(0);
    stream_out.stream_flags.set(0);
    stream_out.current_format = stream_format;
    stream_out.avb_interface_index.set(0);
    stream_out.buffer_length.set(SAMPLES_PER_PACKET * CHANNELS * 4);
    (void)model.add_stream_output(stream_out);

    return model;
}

//
// Components Creation
//

auto AvbEntityStereoIO::create_components() -> nanoavb::NanoAvbComponents
{
    auto entity_model = create_entity_model();
    auto const& entity = entity_model.get_entity();

    // Create AEM command handler
    AemCommandHandler aem_handler{entity_model};

    // Create ADP advertiser
    AdpAdvertiserCallbacks const adp_callbacks{};
    AdpAdvertiserConfig adp_config{};
    adp_config.valid_time = 31;                                        // 62 seconds
    adp_config.reannounce_interval = std::chrono::milliseconds{5000};  // 5 seconds
    NanoAvbAdpAdvertiser adp_advertiser{entity_model.get_entity(), adp_callbacks, adp_config};

    // Create ACMP talker with one stream
    AcmpTalkerCallbacks const talker_callbacks{};
    NanoAvbAcmpTalker acmp_talker{entity.entity_id, talker_callbacks, 1, 4};

    // Configure talker stream with hardwired multicast MAC
    ieee::Eui64 stream_id{};
    span_copy(stream_id.span().first(6),
              entity.entity_id.span().first(6));  // First 6 bytes from entity ID
    (void)acmp_talker.configure_stream(0, stream_id, config_.talker_dest_mac);

    // Create ACMP listener with one stream
    AcmpListenerCallbacks const listener_callbacks{};
    NanoAvbAcmpListener acmp_listener{entity.entity_id, listener_callbacks, 1};

    // Create MVRP handler
    MvrpCallbacks const mvrp_callbacks{};
    MvrpHandler mvrp_handler{statusbar::srp::mvrp::MvrpConfig{}, mvrp_callbacks};
    (void)mvrp_handler.register_vlan(config_.vlan_id, sm::Clock::now());

    // Create MSRP handler
    MsrpCallbacks const msrp_callbacks{};
    MsrpHandler msrp_handler{statusbar::srp::msrp::MsrpConfig{}, msrp_callbacks};
    msrp_handler.set_domain(
        DomainInfo{
            .sr_class_id = 6,                   // SR Class A
            .sr_class_priority = 3,             // Priority 3
            .sr_class_vid = config_.vlan_id});  // VLAN ID

    return NanoAvbComponents{
        .entity_model = std::move(entity_model),
        .aem_handler = std::move(aem_handler),
        .adp_advertiser = std::move(adp_advertiser),
        .acmp_talker = std::move(acmp_talker),
        .acmp_listener = std::move(acmp_listener),
        .mvrp_handler = std::move(mvrp_handler),
        .msrp_handler = std::move(msrp_handler)};
}

//
// State Machine Callback Wiring
//

void AvbEntityStereoIO::wire_callbacks()
{
    //
    // Supervisor callbacks - coordinate protocol lifecycle
    //

    supervisor_ctx_.callbacks.init_iface = [this](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
        // Reset protocol state machines to initial state
        gptp_.reset();
        mvrp_.reset();
    };

    supervisor_ctx_.callbacks.start_protocols = [this](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        // Start gPTP state machine
        gptp_.handle_event(gptp_ctx_, nanoavb::gptp_sm::Def::Event::AsCapableUp, time);
        // Start MVRP state machine
        mvrp_.handle_event(mvrp_ctx_, nanoavb::mvrp_sm::Def::Event::Acquire, time);
    };

    supervisor_ctx_.callbacks.enter_wait_vlan = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
        // MVRP should be registering VLANs at this point
    };

    supervisor_ctx_.callbacks.enter_ready = [this](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        // Start ADP advertising
        auto result = components_.adp_advertiser.start(time);
        if (!result) {
            std::print(stderr, "Warning: ADP start failed: {}\n", result.error().message());
        }
        // Allow stream engines to proceed
        talker_engine_ctx_.send_allowed = true;
        listener_engine_ctx_.play_allowed = true;
    };

    supervisor_ctx_.callbacks.degrade_stop_streams = [this](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        // Stop stream engines
        talker_engine_.handle_event(talker_engine_ctx_, nanoavb::talker_engine_sm::Def::Event::GateStop, time);
        listener_engine_.handle_event(listener_engine_ctx_, nanoavb::listener_engine_sm::Def::Event::GateStop, time);
        talker_engine_ctx_.send_allowed = false;
        listener_engine_ctx_.play_allowed = false;
    };

    supervisor_ctx_.callbacks.stop_all = [this](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        // Stop ADP
        (void)components_.adp_advertiser.stop(time);
        // Stop stream engines
        talker_engine_.handle_event(talker_engine_ctx_, nanoavb::talker_engine_sm::Def::Event::Fatal, time);
        listener_engine_.handle_event(listener_engine_ctx_, nanoavb::listener_engine_sm::Def::Event::GateStop, time);
        // Reset protocol state machines
        gptp_.reset();
        mvrp_.reset();
        msrp_talker_.reset();
        msrp_listener_.reset();
        talker_engine_ctx_.send_allowed = false;
        listener_engine_ctx_.play_allowed = false;
    };

    supervisor_ctx_.callbacks.timeout_gptp = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
        std::print(stderr, "[supervisor] gPTP lock timeout\n");
    };

    supervisor_ctx_.callbacks.timeout_vlan = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
        std::print(stderr, "[supervisor] VLAN registration timeout\n");
    };

    //
    // gPTP callbacks - notify supervisor of sync status
    //

    gptp_ctx_.callbacks.init = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    gptp_ctx_.callbacks.start_servo = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    gptp_ctx_.callbacks.report_locked = [this](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        supervisor_.handle_event(supervisor_ctx_, nanoavb::supervisor_sm::Def::Event::GptpLocked, time);
    };

    gptp_ctx_.callbacks.report_unlocked = [this](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        supervisor_.handle_event(supervisor_ctx_, nanoavb::supervisor_sm::Def::Event::GptpLost, time);
    };

    //
    // MVRP callbacks - notify supervisor of VLAN registration
    //

    mvrp_ctx_.callbacks.init = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    mvrp_ctx_.callbacks.send_join = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    mvrp_ctx_.callbacks.mark_joined = [this](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        supervisor_ctx_.vlan_base_ready = true;
        supervisor_.handle_event(supervisor_ctx_, nanoavb::supervisor_sm::Def::Event::VlanBaseReady, time);
    };

    mvrp_ctx_.callbacks.mark_error = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
        std::print(stderr, "[mvrp] VLAN registration failed\n");
    };

    mvrp_ctx_.callbacks.send_leave = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    mvrp_ctx_.callbacks.mark_left = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    mvrp_ctx_.callbacks.reset = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    //
    // MSRP Talker callbacks
    //

    msrp_talker_ctx_.callbacks.init = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    msrp_talker_ctx_.callbacks.msrp_talker_advertise = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    msrp_talker_ctx_.callbacks.mark_ready = [this](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        if (supervisor_.current_state() == nanoavb::supervisor_sm::Def::State::Ready) {
            talker_engine_.handle_event(talker_engine_ctx_, nanoavb::talker_engine_sm::Def::Event::AudioReady, time);
        }
    };

    msrp_talker_ctx_.callbacks.mark_failed = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    msrp_talker_ctx_.callbacks.msrp_talker_withdraw = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    msrp_talker_ctx_.callbacks.mark_idle = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    //
    // MSRP Listener callbacks
    //

    msrp_listener_ctx_.callbacks.init = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    msrp_listener_ctx_.callbacks.msrp_listener_ready = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    msrp_listener_ctx_.callbacks.mark_ready = [this](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        if (supervisor_.current_state() == nanoavb::supervisor_sm::Def::State::Ready) {
            listener_engine_.handle_event(listener_engine_ctx_, nanoavb::listener_engine_sm::Def::Event::GateListen, time);
        }
    };

    msrp_listener_ctx_.callbacks.mark_failed = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    msrp_listener_ctx_.callbacks.msrp_listener_leave = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    msrp_listener_ctx_.callbacks.mark_idle = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    //
    // Talker Engine callbacks - audio TX pipeline
    //

    talker_engine_ctx_.callbacks.init = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    talker_engine_ctx_.callbacks.start_audio_source = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    talker_engine_ctx_.callbacks.arm_stream = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    talker_engine_ctx_.callbacks.start_tx = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    talker_engine_ctx_.callbacks.stop_tx = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    talker_engine_ctx_.callbacks.mute_tx = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    talker_engine_ctx_.callbacks.unmute_tx = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    talker_engine_ctx_.callbacks.stop_all = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    //
    // Listener Engine callbacks - audio RX pipeline
    //

    listener_engine_ctx_.callbacks.init = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    listener_engine_ctx_.callbacks.enable_rx_filter = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    listener_engine_ctx_.callbacks.start_sync = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    listener_engine_ctx_.callbacks.start_audio_sink = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    listener_engine_ctx_.callbacks.stop_all = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    listener_engine_ctx_.callbacks.resync = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    listener_engine_ctx_.callbacks.mute_out = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    listener_engine_ctx_.callbacks.unmute_out = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };
}

//
// Start / Stop
//

auto AvbEntityStereoIO::start(net::MessageReactor& reactor) -> Status
{
    if (running_) {
        return failure(std::make_error_code(std::errc::already_connected));
    }

    // Create network handlers
    net_handlers_ = std::make_unique<NanoAvbNetHandlers>(config_.interface_name, components_);
    net_handlers_->print_warnings(config_.interface_name);

    // Wire up callbacks between components and network handlers
    setup_nanoavb_callbacks(components_, *net_handlers_);

    // Wire up state machine callbacks
    wire_callbacks();

    // Add network handlers to reactor
    net_handlers_->add_to_reactor(reactor);

    running_ = true;
    return success();
}

auto AvbEntityStereoIO::stop() -> Status
{
    if (!running_) {
        return failure(std::make_error_code(std::errc::not_connected));
    }

    // Stop ADP advertising
    auto const now = TimePoint{std::chrono::steady_clock::now().time_since_epoch()};
    (void)components_.adp_advertiser.stop(now);

    // Clean up network handlers
    net_handlers_.reset();

    running_ = false;
    return success();
}

//
// State Queries
//

auto AvbEntityStereoIO::is_ready() const noexcept -> bool
{
    return supervisor_.current_state() == nanoavb::supervisor_sm::Def::State::Ready;
}

auto AvbEntityStereoIO::state_string() const -> std::string
{
    using nanoavb::supervisor_sm::Def;
    auto const state = supervisor_.current_state();

    switch (state) {
        case Def::State::Start:
            return "Start";
        case Def::State::Down:
            return "Down";
        case Def::State::Init:
            return "Init";
        case Def::State::WaitVlanBase:
            return "WaitVlanBase";
        case Def::State::Ready:
            return "Ready";
        case Def::State::Degraded:
            return "Degraded";
        default:
            return "Unknown";
    }
}

void AvbEntityStereoIO::print_state() const
{
    std::print(
        "State: supervisor={} gptp={} mvrp={}\n",
        state_string(),
        gptp_ctx_.time_locked ? "Locked" : "Unlocked",
        mvrp_ctx_.joined ? "Joined" : "NotJoined");
}

//
// Event Handlers
//

void AvbEntityStereoIO::on_link_up(TimePoint time)
{
    supervisor_ctx_.link_up = true;
    supervisor_.handle_event(supervisor_ctx_, nanoavb::supervisor_sm::Def::Event::LinkUp, time);
}

void AvbEntityStereoIO::on_link_down(TimePoint time)
{
    supervisor_ctx_.link_up = false;
    supervisor_.handle_event(supervisor_ctx_, nanoavb::supervisor_sm::Def::Event::LinkDown, time);
}

void AvbEntityStereoIO::on_gptp_announce(TimePoint time, bool has_grandmaster)
{
    if (has_grandmaster && !gptp_ctx_.time_locked) {
        gptp_.handle_event(gptp_ctx_, nanoavb::gptp_sm::Def::Event::LockedStable, time);
    }
}

void AvbEntityStereoIO::on_timeout(TimePoint time)
{
    using nanoavb::supervisor_sm::Def;
    auto const state = supervisor_.current_state();

    if (state == Def::State::Init || state == Def::State::WaitVlanBase) {
        supervisor_.handle_event(supervisor_ctx_, Def::Event::Timeout, time);
    }
}

//
// Audio Processing
//

void AvbEntityStereoIO::process_audio(TimePoint time)
{
    (void)time;

    // Process through biquad filters (left and right channels)
    for (size_t i = 0; i < SAMPLES_PER_PACKET; ++i) {
        // Left channel (even indices)
        audio_buffer_[(i * 2)] = biquad_left_(audio_buffer_[(i * 2)]);
        // Right channel (odd indices)
        audio_buffer_[(i * 2) + 1] = biquad_right_(audio_buffer_[(i * 2) + 1]);
    }

    // Call custom audio callback if set
    if (audio_callback_) {
        audio_callback_(std::span{audio_buffer_}, SAMPLES_PER_PACKET);
    }
}

void AvbEntityStereoIO::configure_filter(double const freq_hz, double const gain_db, double const q)
{
    double const sample_rate_recip = 1.0 / static_cast<double>(SAMPLE_RATE);

    // Configure peak EQ filter for both channels
    // Negative gain = cut, positive gain = boost
    biquad_left_.coeffs.calculate_peak(
        {.sample_rate_recip = sample_rate_recip, .frequency = freq_hz, .q = q, .gain_db = gain_db, .channel = 0});
    biquad_right_.coeffs.calculate_peak(
        {.sample_rate_recip = sample_rate_recip, .frequency = freq_hz, .q = q, .gain_db = gain_db, .channel = 0});

    // Reset filter states
    biquad_left_.reset();
    biquad_right_.reset();
}

void AvbEntityStereoIO::process_listener_packet(std::span<uint8_t const> packet, TimePoint time)
{
    auto const header = avtp::am824_parse_header(packet);
    if (!header) {
        return;  // malformed AM824 packet
    }

    auto const payload = avtp::am824_get_audio_payload(packet);
    if (payload.empty()) {
        return;
    }

    auto const samples_per_channel = avtp::am824_deserialize_interleaved(
        payload, static_cast<uint8_t>(CHANNELS), static_cast<uint8_t>(SAMPLES_PER_PACKET), std::span{audio_buffer_});

    if (samples_per_channel > 0) {
        process_audio(time);
    }
}

}  // namespace statusbar::avb_entity
