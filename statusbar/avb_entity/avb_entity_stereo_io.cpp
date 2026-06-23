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
#include "statusbar/status/catch_or_status.hpp"
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

// ADP advertiser configuration for this entity (valid_time + reannounce).
static auto make_adp_config() -> AdpAdvertiserConfig
{
    AdpAdvertiserConfig adp_config{};
    adp_config.valid_time = 31;                                        // 62 seconds
    adp_config.reannounce_interval = std::chrono::milliseconds{5000};  // 5 seconds
    return adp_config;
}

AvbEntityStereoIO::AvbEntityStereoIO(AvbEntityStereoIOConfig config)
    // Build the control plane host in place from the hand-built model: 1 talker
    // stream (4 max listeners), 1 listener stream. create_entity_model() reads
    // config_ (constructed first); the host's NanoAvbComponents is non-movable.
    : config_{std::move(config)}
    , host_{create_entity_model(), make_adp_config(), 1, 4, 1}
{
    // Configure biquad filters for both channels
    configure_filter(config_.filter_freq_hz, config_.filter_gain_db, config_.filter_q);

    // Derive talker stream ID from entity ID (entity_id + unique_id 0x0000)
    talker_stream_id_ = config_.entity_id;

    // Post-construction setup that needs config-supplied values.
    auto const& entity = host_.components().entity_model.get_entity();

    // Configure talker stream 0 with the hardwired multicast destination MAC.
    ieee::Eui64 stream_id{};
    span_copy(stream_id.span().first(6), entity.entity_id.span().first(6));
    (void)host_.components().acmp_talker.configure_stream(0, stream_id, config_.talker_dest_mac);

    // Register the VLAN with MVRP and set the MSRP SR-class domain.
    (void)host_.components().mvrp_handler.register_vlan(config_.vlan_id, sm::Clock::now());
    host_.components().msrp_handler.set_domain(DomainInfo{
        .sr_class_id = 6,                   // SR Class A
        .sr_class_priority = 3,             // Priority 3
        .sr_class_vid = config_.vlan_id});  // VLAN ID
}

AvbEntityStereoIO::~AvbEntityStereoIO()
{
    // Destructors must not throw; catch_or_status contains any exception from
    // stop() (and compiles to a plain call under -fno-exceptions).
    (void)statusbar::catch_or_status(
        [&]() -> statusbar::Status {
            if (host_.is_running()) {
                (void)stop();
            }
            return {};
        },
        std::errc::io_error);
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
// Stream-specific control-plane wiring (the host owns the generic SM wiring)
//

void AvbEntityStereoIO::wire_stream_callbacks()
{
    // One talker stream. The host declares the SR class domain for us before these
    // fire, then advertises/withdraws on the MSRP cycle. No transmit gate and no
    // listener hook here -- this entity's readiness is the host's generic SM drive.
    host_.set_advertise_streams([this](TimePoint time) {
        auto result = host_.components().msrp_handler.talker_advertise(make_talker_srp_info(), time);
        if (!result) {
            std::print(stderr, "Warning: MSRP talker_advertise failed: {}\n", result.error().message());
        }
    });
    host_.set_withdraw_streams(
        [this](TimePoint time) { (void)host_.components().msrp_handler.talker_withdraw(make_talker_srp_info().stream_id, time); });

    // ACMP: observe controller-initiated connections to our talker (diagnostic;
    // streaming is gated by MSRP listener-ready + gPTP).
    host_.components().acmp_talker.set_connection_callbacks(
        [](uint16_t stream_index, ieee::Eui64 listener_entity_id, uint16_t listener_unique_id) {
            std::print(
                "[acmp] talker stream {} CONNECTED  by listener {:012x} unique_id {}\n",
                stream_index,
                listener_entity_id.to_uint64(),
                listener_unique_id);
        },
        [](uint16_t stream_index, ieee::Eui64 listener_entity_id, uint16_t listener_unique_id) {
            std::print(
                "[acmp] talker stream {} DISCONNECTED by listener {:012x} unique_id {}\n",
                stream_index,
                listener_entity_id.to_uint64(),
                listener_unique_id);
        });
}

//
// Start / Stop
//

auto AvbEntityStereoIO::start(net::MessageReactor& reactor) -> Status
{
    // Bring up the shared control plane (net handlers + generic SM wiring), then
    // attach this entity's stream-specific callbacks. The host has no data plane of
    // its own to set up here -- this entity's TX/RX runs through the nanoavb handlers.
    if (auto status = host_.start_control_plane(reactor, config_.interface_name); !status) {
        return status;
    }
    wire_stream_callbacks();
    return success();
}

auto AvbEntityStereoIO::stop() -> Status
{
    if (!host_.is_running()) {
        return failure(std::make_error_code(std::errc::not_connected));
    }
    auto const now = TimePoint{std::chrono::steady_clock::now().time_since_epoch()};
    return host_.stop_control_plane(now);
}

//
// State Queries
//

void AvbEntityStereoIO::print_state() const
{
    std::print(
        "State: supervisor={} gptp={} mvrp={} acmp_connections={}\n",
        state_string(),
        host_.gptp_locked() ? "Locked" : "Unlocked",
        host_.mvrp_joined() ? "Joined" : "NotJoined",
        host_.components().acmp_talker.connection_count(0));
}

//
// MSRP talker reservation
//

auto AvbEntityStereoIO::make_talker_srp_info() const -> nanoavb::TalkerStreamSrpInfo
{
    nanoavb::TalkerStreamSrpInfo info{};

    // Source stream id / destination / VLAN from the ACMP-configured talker
    // stream 0 so MSRP advertises the exact same stream the controller sees.
    if (auto const* stream = host_.components().acmp_talker.get_stream(0); stream != nullptr) {
        (void)statusbar::tsn::load_unchecked(stream->stream_id.span(), &info.stream_id);
        info.dest_address = stream->stream_dest_mac;
        info.vlan_id = stream->stream_vlan_id;
    } else {
        info.vlan_id = config_.vlan_id;
    }

    // SR class A: one AVTP packet per 125 us measurement interval.
    info.max_interval_frames = 1;

    // max_frame_size is the L2 payload (MSDU): AVTP stream header (24) + CIP
    // header (8) = 32 bytes, then SAMPLES_PER_PACKET AM824 quadlets per channel
    // at 4 bytes each.
    constexpr uint16_t AVTP_AM824_HEADER_BYTES = 32;
    constexpr uint16_t AM824_QUADLET_BYTES = 4;
    info.max_frame_size = static_cast<uint16_t>(AVTP_AM824_HEADER_BYTES + (SAMPLES_PER_PACKET * CHANNELS * AM824_QUADLET_BYTES));

    // We are the media source; downstream bridges accumulate their own latency.
    info.accumulated_latency = 0;
    return info;
}

//
// Event Handlers
//

// The entity's events drive the shared SM stack, owned by the host.
void AvbEntityStereoIO::on_link_up(TimePoint time)
{
    host_.on_link_up(time);
}
void AvbEntityStereoIO::on_link_down(TimePoint time)
{
    host_.on_link_down(time);
}
void AvbEntityStereoIO::on_gptp_announce(TimePoint time, bool has_grandmaster)
{
    host_.on_gptp_announce(time, has_grandmaster);
}
void AvbEntityStereoIO::on_timeout(TimePoint time)
{
    host_.on_timeout(time);
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
