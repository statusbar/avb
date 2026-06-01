// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// AVB Entity AM824 I/O Implementation
/// Implements the AvbEntityAm824IO class with blob-loaded entity model and N-channel DSP

#include "statusbar/avb_entity/avb_entity_am824_io.hpp"

#include "statusbar/atdecc/atdecc.hpp"
#include "statusbar/atdecc/atdecc_adp.hpp"
#include "statusbar/atdecc/atdecc_aem_descriptor.hpp"
#include "statusbar/atdecc/atdecc_descriptor_storage.hpp"
#include "statusbar/buffer/buffer.hpp"
#include "statusbar/buffer/span_utils.hpp"
#include "statusbar/dsp/dsp.hpp"
#include "statusbar/dsp/dsp_biquad.hpp"
#include "statusbar/ieee/ieee.hpp"
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
#include "statusbar/sm/sm.hpp"
#include "statusbar/status/status.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <functional>
#include <memory>
#include <numbers>
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

namespace {

/// Reactor port that receives AVTP AM824 stream frames on a dedicated socket
/// joined to the talker stream's multicast group, and hands each frame to the
/// owning entity for decode/metering. Owns its own RawnetContext so the stream
/// RX path is independent of the ATDECC control socket.
class StreamRxHandler : public net::Pollable
{
  public:
    StreamRxHandler(std::string_view interface_name, ieee::Eui48 const& group, AvbEntityAm824IO* owner)
        : owner_{owner}
    {
        // Join the stream multicast group; the NIC's multicast hash filter
        // delivers it to this (non-promiscuous) socket — verified on the Pi5
        // macb NIC. (Promiscuous mode was briefly used as a workaround but is
        // unnecessary, and its full-wire flood preempted the PTP time-bridge
        // sampler thread, which destabilised the media clock and caused bursty
        // streaming.)
        (void)sock_.open(interface_name, avtp::AVTP_ETHERTYPE, &group, /*qdisc_bypass=*/false);
    }

    [[nodiscard]] auto valid() const noexcept -> bool { return sock_.fd() >= 0; }

    [[nodiscard]] auto fd() const noexcept -> int override { return sock_.fd(); }

    void on_ready(int64_t now_ns) override
    {
        ieee::Eui48 src{};
        ieee::Eui48 dst{};
        while (true) {
            auto const r = sock_.recv(&src, &dst, buf_);
            if (!r || *r <= 0) {
                break;
            }
            owner_->on_stream_rx_frame({buf_.data(), static_cast<size_t>(*r)}, now_ns);
        }
    }

    void tick(int64_t /*now_ns*/) override {}

    [[nodiscard]] auto finished() const noexcept -> bool override { return false; }

  private:
    net::RawnetContext sock_{};
    std::array<uint8_t, 2048> buf_{};
    AvbEntityAm824IO* owner_;
};

}  // namespace

//
// File-static helpers
//

/// Load a single AEM descriptor of type T from storage.
/// Accepts blobs smaller than sizeof(T) via partial-load fallback.
template <typename T>
static auto load_descriptor(DescriptorStorage const& storage, uint16_t config_idx, uint16_t type, uint16_t index) -> StatusValue<T>
{
    auto result = storage.get_descriptor(config_idx, type, index);
    if (!result) {
        return failure(result.error());
    }
    auto const blob = *result;
    T desc{};
    span_load_padded(desc, blob);
    return success(desc);
}

/// Count how many descriptors of a given type exist in a configuration.
static auto count_descriptors(DescriptorStorage const& storage, uint16_t config_idx, uint16_t type) -> size_t
{
    size_t count = 0;
    while (true) {
        auto result = storage.get_descriptor(config_idx, type, static_cast<uint16_t>(count));
        if (!result) {
            break;
        }
        ++count;
    }
    return count;
}

/// Build an EntityModel from a validated DescriptorStorage, overriding runtime fields from config.
/// Returns the built model and channel count extracted from the first audio cluster.
static auto build_entity_model(DescriptorStorage const& storage, AvbEntityAm824IOConfig const& config, size_t& out_channels)
    -> StatusValue<EntityModel>
{
    uint16_t const config_idx = 0;

    // Count all descriptor types
    auto const n_configurations = count_descriptors(storage, config_idx, DESCRIPTOR_CONFIGURATION);
    auto const n_audio_units = count_descriptors(storage, config_idx, DESCRIPTOR_AUDIO_UNIT);
    auto const n_stream_inputs = count_descriptors(storage, config_idx, DESCRIPTOR_STREAM_INPUT);
    auto const n_stream_outputs = count_descriptors(storage, config_idx, DESCRIPTOR_STREAM_OUTPUT);
    auto const n_jack_inputs = count_descriptors(storage, config_idx, DESCRIPTOR_JACK_INPUT);
    auto const n_jack_outputs = count_descriptors(storage, config_idx, DESCRIPTOR_JACK_OUTPUT);
    auto const n_avb_interfaces = count_descriptors(storage, config_idx, DESCRIPTOR_AVB_INTERFACE);
    auto const n_clock_sources = count_descriptors(storage, config_idx, DESCRIPTOR_CLOCK_SOURCE);
    auto const n_clock_domains = count_descriptors(storage, config_idx, DESCRIPTOR_CLOCK_DOMAIN);
    auto const n_locales = count_descriptors(storage, config_idx, DESCRIPTOR_LOCALE);
    auto const n_strings = count_descriptors(storage, config_idx, DESCRIPTOR_STRINGS);
    auto const n_stream_port_inputs = count_descriptors(storage, config_idx, DESCRIPTOR_STREAM_PORT_INPUT);
    auto const n_stream_port_outputs = count_descriptors(storage, config_idx, DESCRIPTOR_STREAM_PORT_OUTPUT);
    auto const n_audio_clusters = count_descriptors(storage, config_idx, DESCRIPTOR_AUDIO_CLUSTER);
    auto const n_audio_maps = count_descriptors(storage, config_idx, DESCRIPTOR_AUDIO_MAP);
    auto const n_controls = count_descriptors(storage, config_idx, DESCRIPTOR_CONTROL);

    // Validate minimum required descriptors
    auto has_minimum_descriptors = [](size_t streams_in, size_t streams_out, size_t avb_ifaces) -> bool {
        return streams_in >= 1 && streams_out >= 1 && avb_ifaces >= 1;
    };
    if (!has_minimum_descriptors(n_stream_inputs, n_stream_outputs, n_avb_interfaces)) {
        return failure(BufferError::insufficient_data);
    }

    // Build EntityModelConfig sized to hold the blob's descriptors
    EntityModelConfig model_cfg{};
    model_cfg.max_configurations = std::max(n_configurations, size_t{1});
    model_cfg.max_audio_units = std::max(n_audio_units, size_t{1});
    model_cfg.max_stream_inputs = std::max(n_stream_inputs, size_t{1});
    model_cfg.max_stream_outputs = std::max(n_stream_outputs, size_t{1});
    model_cfg.max_jack_inputs = std::max(n_jack_inputs, size_t{1});
    model_cfg.max_jack_outputs = std::max(n_jack_outputs, size_t{1});
    model_cfg.max_avb_interfaces = std::max(n_avb_interfaces, size_t{1});
    model_cfg.max_clock_sources = std::max(n_clock_sources, size_t{1});
    model_cfg.max_clock_domains = std::max(n_clock_domains, size_t{1});
    model_cfg.max_locales = std::max(n_locales, size_t{1});
    model_cfg.max_strings = std::max(n_strings, size_t{1});
    model_cfg.max_stream_port_inputs = std::max(n_stream_port_inputs, size_t{1});
    model_cfg.max_stream_port_outputs = std::max(n_stream_port_outputs, size_t{1});
    model_cfg.max_audio_clusters = std::max(n_audio_clusters, size_t{1});
    model_cfg.max_audio_maps = std::max(n_audio_maps, size_t{1});
    model_cfg.max_controls = std::max(n_controls, size_t{1});

    EntityModel model{model_cfg};

    // Load and set entity descriptor, overriding runtime fields from config
    {
        auto r = load_descriptor<DescriptorEntity>(storage, config_idx, DESCRIPTOR_ENTITY, 0);
        if (r) {
            r->entity_id = config.entity_id;
            r->entity_model_id = config.entity_model_id;
            r->entity_name = AtdeccString{config.entity_name.c_str()};
            r->firmware_version = AtdeccString{config.firmware_version.c_str()};
            model.set_entity(*r);
        }
    }

    // Populate all descriptor types
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_configurations); ++i) {
        auto r = load_descriptor<DescriptorConfiguration>(storage, config_idx, DESCRIPTOR_CONFIGURATION, i);
        if (r) {
            (void)model.add_configuration(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_audio_units); ++i) {
        auto r = load_descriptor<DescriptorAudioUnit>(storage, config_idx, DESCRIPTOR_AUDIO_UNIT, i);
        if (r) {
            (void)model.add_audio_unit(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_stream_inputs); ++i) {
        auto r = load_descriptor<DescriptorStream>(storage, config_idx, DESCRIPTOR_STREAM_INPUT, i);
        if (r) {
            (void)model.add_stream_input(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_stream_outputs); ++i) {
        auto r = load_descriptor<DescriptorStream>(storage, config_idx, DESCRIPTOR_STREAM_OUTPUT, i);
        if (r) {
            (void)model.add_stream_output(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_jack_inputs); ++i) {
        auto r = load_descriptor<DescriptorJack>(storage, config_idx, DESCRIPTOR_JACK_INPUT, i);
        if (r) {
            (void)model.add_jack_input(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_jack_outputs); ++i) {
        auto r = load_descriptor<DescriptorJack>(storage, config_idx, DESCRIPTOR_JACK_OUTPUT, i);
        if (r) {
            (void)model.add_jack_output(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_avb_interfaces); ++i) {
        auto r = load_descriptor<DescriptorAvbInterface>(storage, config_idx, DESCRIPTOR_AVB_INTERFACE, i);
        if (r) {
            (void)model.add_avb_interface(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_clock_sources); ++i) {
        auto r = load_descriptor<DescriptorClockSource>(storage, config_idx, DESCRIPTOR_CLOCK_SOURCE, i);
        if (r) {
            (void)model.add_clock_source(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_clock_domains); ++i) {
        auto r = load_descriptor<DescriptorClockDomain>(storage, config_idx, DESCRIPTOR_CLOCK_DOMAIN, i);
        if (r) {
            (void)model.add_clock_domain(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_locales); ++i) {
        auto r = load_descriptor<DescriptorLocale>(storage, config_idx, DESCRIPTOR_LOCALE, i);
        if (r) {
            (void)model.add_locale(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_strings); ++i) {
        auto r = load_descriptor<DescriptorStrings>(storage, config_idx, DESCRIPTOR_STRINGS, i);
        if (r) {
            (void)model.add_strings(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_stream_port_inputs); ++i) {
        auto r = load_descriptor<DescriptorStreamPort>(storage, config_idx, DESCRIPTOR_STREAM_PORT_INPUT, i);
        if (r) {
            (void)model.add_stream_port_input(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_stream_port_outputs); ++i) {
        auto r = load_descriptor<DescriptorStreamPort>(storage, config_idx, DESCRIPTOR_STREAM_PORT_OUTPUT, i);
        if (r) {
            (void)model.add_stream_port_output(*r);
        }
    }

    // Extract channel count from first audio cluster (before adding to model)
    out_channels = 2;  // Default: stereo fallback
    {
        auto r = load_descriptor<DescriptorAudioCluster>(storage, config_idx, DESCRIPTOR_AUDIO_CLUSTER, 0);
        if (r) {
            auto const ch = static_cast<uint16_t>(r->channel_count);
            if (ch >= 1) {
                out_channels = static_cast<size_t>(ch);
            }
        }
    }

    for (uint16_t i = 0; i < static_cast<uint16_t>(n_audio_clusters); ++i) {
        auto r = load_descriptor<DescriptorAudioCluster>(storage, config_idx, DESCRIPTOR_AUDIO_CLUSTER, i);
        if (r) {
            (void)model.add_audio_cluster(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_audio_maps); ++i) {
        auto r = load_descriptor<DescriptorAudioMap>(storage, config_idx, DESCRIPTOR_AUDIO_MAP, i);
        if (r) {
            (void)model.add_audio_map(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_controls); ++i) {
        auto r = load_descriptor<DescriptorControl>(storage, config_idx, DESCRIPTOR_CONTROL, i);
        if (r) {
            (void)model.add_control(*r);
        }
    }

    return success(std::move(model));
}

/// ADP advertiser configuration for this entity (valid_time + reannounce).
static auto make_adp_config() -> AdpAdvertiserConfig
{
    AdpAdvertiserConfig adp_config{};
    adp_config.valid_time = 31;                                        // 62 seconds
    adp_config.reannounce_interval = std::chrono::milliseconds{5000};  // 5 seconds
    return adp_config;
}

//
// Factory method
//

auto AvbEntityAm824IO::create(AvbEntityAm824IOConfig config, std::pmr::memory_resource* memory_resource)
    -> StatusValue<std::unique_ptr<AvbEntityAm824IO>>
{
    // Step 1: Validate the descriptor storage blob
    auto storage_result = DescriptorStorage::create(std::span<uint8_t const>{config.descriptor_storage_blob});
    if (!storage_result) {
        return failure(storage_result.error());
    }

    // Step 2: Build EntityModel from blob, extract channel count
    size_t channels = 2;
    auto model_result = build_entity_model(*storage_result, config, channels);
    if (!model_result) {
        return failure(model_result.error());
    }

    // Step 3: Construct entity via make_unique (gated by CreateKey). The
    // entity owns the model and builds NanoAvbComponents in place (it is
    // non-movable), then wires the talker stream / VLAN / MSRP domain.
    std::pmr::memory_resource* const mr = memory_resource != nullptr ? memory_resource : std::pmr::get_default_resource();
    auto entity = std::make_unique<AvbEntityAm824IO>(
        AvbEntityAm824IO::CreateKey{}, std::move(config), std::move(*model_result), channels, mr);

    // Step 5: Configure filter and set talker stream ID
    entity->configure_filter(entity->config_.filter_freq_hz, entity->config_.filter_gain_db, entity->config_.filter_q);

    entity->talker_stream_id_ = entity->config_.entity_id;

    return success(std::move(entity));
}

//
// Constructor / Destructor
//

AvbEntityAm824IO::AvbEntityAm824IO(
    CreateKey,
    AvbEntityAm824IOConfig config,
    nanoavb::EntityModel entity_model,
    size_t channels,
    std::pmr::memory_resource* memory_resource)
    : config_{std::move(config)}  // Construct components_ in place: 1 talker stream (4 max listeners),
    // 1 listener stream. NanoAvbComponents wires its handlers against its own
    // entity_model member; no move of the components ever happens.
    , components_{std::move(entity_model), make_adp_config(), 1, 4, 1}
    , channels_{channels}
    , mem_resource_{memory_resource}
    , biquads_(channels, dsp::BiQuad<float>{}, mem_resource_)
    , audio_buffer_(SAMPLES_PER_PACKET * channels, 0.0f, mem_resource_)
    , oscillators_(channels, dsp::Oscillator<float>{}, mem_resource_)
{
    // Configure the per-channel sine source: identical frequency, but a distinct
    // initial phase per channel so no two channels carry the same signal.
    double const sr_recip = 1.0 / static_cast<double>(SAMPLE_RATE);
    for (size_t ch = 0; ch < channels_; ++ch) {
        double const phase =
            channels_ > 0 ? (2.0 * std::numbers::pi * static_cast<double>(ch) / static_cast<double>(channels_)) : 0.0;
        oscillators_[ch].state_.set_frequency(
            dsp::FrequencyParameters<double>{
                .sample_rate_recip = sr_recip, .frequency = config_.tone_freq_hz, .phase_in_radians = phase},
            0);
        oscillators_[ch].coeffs_.set_amplitude(config_.tone_amplitude, 0);
    }

    // Post-construction setup that needs config-supplied values. Done here
    // (not in NanoAvbComponents) because it is entity-specific.
    auto const& entity = components_.entity_model.get_entity();

    // Configure talker stream 0: stream id = entity id (first 6 bytes), with
    // the hardwired multicast destination MAC from config.
    ieee::Eui64 stream_id{};
    span_copy(stream_id.span().first(6), entity.entity_id.span().first(6));
    (void)components_.acmp_talker.configure_stream(0, stream_id, config_.talker_dest_mac);

    // Register the VLAN with MVRP and set the MSRP SR-class domain.
    (void)components_.mvrp_handler.register_vlan(config_.vlan_id, sm::Clock::now());
    components_.msrp_handler.set_domain(
        DomainInfo{
            .sr_class_id = 6,        // SR Class A
            .sr_class_priority = 3,  // Priority 3
            .sr_class_vid = config_.vlan_id});
}

AvbEntityAm824IO::~AvbEntityAm824IO()
{
#if __cpp_exceptions
    try {
#endif
        if (running_) {
            (void)stop();
        }
#if __cpp_exceptions
    } catch (...) {  // NOLINT(bugprone-empty-catch) - destructors must not throw
    }
#endif
}

//
// State Machine Callback Wiring
//

void AvbEntityAm824IO::wire_callbacks()
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
        // Link is up. Start gPTP (runs independently to acquire lock) and start
        // advertising ADP immediately — discovery/enumeration (ADP/AECP/ACMP)
        // is independent of gPTP and SRP. SRP (MVRP + MSRP) is NOT started here;
        // it is gated on gPTP lock and brought up in enter_ready(). stop_all()
        // stops ADP again on link-down.
        gptp_.handle_event(gptp_ctx_, nanoavb::gptp_sm::Def::Event::AsCapableUp, time);
        auto result = components_.adp_advertiser.start(time);
        if (!result) {
            std::print(stderr, "Warning: ADP start failed: {}\n", result.error().message());
        }
    };

    supervisor_ctx_.callbacks.enter_ready = [this](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        // gPTP is locked: bring up SRP and allow the stream engines to run.
        // MVRP VLAN registration is best-effort (streaming is gated on gPTP,
        // not on the VLAN join). ADP/AECP/ACMP are unaffected — live since
        // link-up.
        mvrp_.handle_event(mvrp_ctx_, nanoavb::mvrp_sm::Def::Event::Acquire, time);
        // Advertise our talker stream reservation via MSRP. Gated on gPTP lock
        // (this is the enter_ready hook), so MSRP follows gPTP lock/unlock. The
        // StartAdvertise event drives the msrp_talker_advertise callback, which
        // calls msrp_handler.talker_advertise().
        msrp_talker_.handle_event(msrp_talker_ctx_, nanoavb::msrp_talker_sm::Def::Event::StartAdvertise, time);
        talker_engine_ctx_.send_allowed = true;
        listener_engine_ctx_.play_allowed = true;
    };

    supervisor_ctx_.callbacks.degrade_stop_streams = [this](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        // gPTP lost: tear down SRP (MVRP + MSRP) and stop the stream engines.
        // ADP keeps advertising — the link is still up.
        talker_engine_.handle_event(talker_engine_ctx_, nanoavb::talker_engine_sm::Def::Event::GateStop, time);
        listener_engine_.handle_event(listener_engine_ctx_, nanoavb::listener_engine_sm::Def::Event::GateStop, time);
        talker_engine_ctx_.send_allowed = false;
        listener_engine_ctx_.play_allowed = false;
        // Withdraw the MSRP talker reservation (StopAdvertise -> withdraw
        // callback -> msrp_handler.talker_withdraw) before resetting the SMs.
        msrp_talker_.handle_event(msrp_talker_ctx_, nanoavb::msrp_talker_sm::Def::Event::StopAdvertise, time);
        mvrp_.reset();
        msrp_talker_.reset();
        msrp_listener_.reset();
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

    mvrp_ctx_.callbacks.mark_joined = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
        // VLAN registered with the bridge. Best-effort: streaming is gated on
        // gPTP lock (enter_ready), not on this join, so nothing to do here.
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

    msrp_talker_ctx_.callbacks.msrp_talker_advertise = [this](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        // Register/advertise our talker stream reservation with MSRP.
        auto result = components_.msrp_handler.talker_advertise(make_talker_srp_info(), time);
        if (!result) {
            std::print(stderr, "Warning: MSRP talker_advertise failed: {}\n", result.error().message());
        }
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

    msrp_talker_ctx_.callbacks.msrp_talker_withdraw = [this](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        // Withdraw our talker stream reservation from MSRP.
        (void)components_.msrp_handler.talker_withdraw(make_talker_srp_info().stream_id, time);
    };

    msrp_talker_ctx_.callbacks.mark_idle = [](auto& ctx, TimePoint time) -> void {
        (void)ctx;
        (void)time;
    };

    // Inbound MSRP: when a remote listener becomes ready (or stops being ready)
    // for our advertised talker stream, drive the MSRP talker SM. Ready ->
    // Ready (mark_ready fires the talker engine AudioReady); not-ready -> Lost
    // (back to Advertising). The SM ignores events invalid for its state.
    components_.msrp_handler.set_on_talker_listener([this](nanoavb::StreamId const& /*stream_id*/, bool ready) {
        auto const now = sm::Clock::now();
        msrp_talker_.handle_event(
            msrp_talker_ctx_, ready ? nanoavb::msrp_talker_sm::Def::Event::Ready : nanoavb::msrp_talker_sm::Def::Event::Lost, now);
    });

    // ACMP: observe controller-initiated stream connections to our talker. The
    // talker SM already answers CONNECT_TX/DISCONNECT_TX and tracks the
    // connection count; here we surface connect/disconnect for diagnostics.
    // Streaming itself is gated by MSRP listener-ready (above) + gPTP, so this
    // is informational. set_connection_callbacks preserves the tx_response wired
    // by setup_nanoavb_callbacks.
    components_.acmp_talker.set_connection_callbacks(
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

auto AvbEntityAm824IO::start(net::MessageReactor& reactor) -> Status
{
    if (running_) {
        return failure(std::make_error_code(std::errc::already_connected));
    }

    // Create the handler container, then construct the per-protocol handlers
    // (which open their raw sockets) and register them with the reactor FIRST.
    // print_warnings() and setup_nanoavb_callbacks() both inspect/wire the
    // constructed handlers, so they must run after add_to_reactor() — otherwise
    // they dereference the still-null handler pointers (segfault) and report
    // spurious "could not open socket" warnings before the sockets exist.
    net_handlers_ = std::make_unique<NanoAvbNetHandlers>(config_.interface_name, components_);
    net_handlers_->add_to_reactor(reactor);
    net_handlers_->print_warnings(config_.interface_name);

    // Wire up callbacks between components and network handlers
    setup_nanoavb_callbacks(components_, *net_handlers_);

    // Wire up state machine callbacks
    wire_callbacks();

    // --- Stream data plane ---
    // Resolve the talker stream identity (stream id + dest MAC) from ACMP
    // stream 0 so the AVTP stream, MSRP reservation, and ACMP all agree.
    statusbar::tsn::StreamId sid{};
    if (auto const* s = components_.acmp_talker.get_stream(0); s != nullptr) {
        (void)statusbar::tsn::load_unchecked(s->stream_id.span(), &sid);
        stream_dest_mac_ = s->stream_dest_mac;
    }
    talker_out_.emplace(sid, avtp::Am824SampleRate::rate_96_khz, static_cast<uint8_t>(channels_), PRESENTATION_OFFSET_NS);
    listener_in_.emplace(avtp::Am824SampleRate::rate_96_khz, static_cast<uint8_t>(channels_));

    // Transmit socket (PTP-thread egress). qdisc-bypass so we do not re-receive
    // our own stream frames on this host.
    (void)stream_tx_.open(config_.interface_name, avtp::AVTP_ETHERTYPE, nullptr, /*qdisc_bypass=*/true);

    // Receive port: join the stream multicast group and decode incoming AM824.
    auto rx = std::make_unique<StreamRxHandler>(config_.interface_name, stream_dest_mac_, this);
    if (rx->valid()) {
        reactor.add(std::move(rx));
    }

    running_ = true;
    return success();
}

auto AvbEntityAm824IO::stop() -> Status
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

auto AvbEntityAm824IO::is_ready() const noexcept -> bool
{
    return supervisor_.current_state() == nanoavb::supervisor_sm::Def::State::Ready;
}

auto AvbEntityAm824IO::state_string() const -> std::string_view
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
        case Def::State::Ready:
            return "Ready";
        case Def::State::Degraded:
            return "Degraded";
        default:
            return "Unknown";
    }
}

void AvbEntityAm824IO::print_state() const
{
    std::print(
        "State: supervisor={} gptp={} mvrp={} acmp_connections={} channels={} stream_tx={} stream_rx={} rx_samples={} "
        "rx_bad={}\n",
        state_string(),
        gptp_ctx_.time_locked ? "Locked" : "Unlocked",
        mvrp_ctx_.joined ? "Joined" : "NotJoined",
        components_.acmp_talker.connection_count(0),
        channels_,
        stream_tx_packets_,
        stream_rx_packets_.load(std::memory_order_relaxed),
        stream_rx_samples_.load(std::memory_order_relaxed),
        stream_rx_bad_.load(std::memory_order_relaxed));
}

//
// MSRP talker reservation
//

auto AvbEntityAm824IO::make_talker_srp_info() const -> nanoavb::TalkerStreamSrpInfo
{
    nanoavb::TalkerStreamSrpInfo info{};

    // Source stream id / destination / VLAN from the ACMP-configured talker
    // stream 0 so MSRP advertises the exact same stream the controller sees.
    if (auto const* stream = components_.acmp_talker.get_stream(0); stream != nullptr) {
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
    // at 4 bytes each (12 samples x channels x 4 at 96 kHz).
    constexpr uint16_t AVTP_AM824_HEADER_BYTES = 32;
    constexpr uint16_t AM824_QUADLET_BYTES = 4;
    info.max_frame_size = static_cast<uint16_t>(AVTP_AM824_HEADER_BYTES + (SAMPLES_PER_PACKET * channels_ * AM824_QUADLET_BYTES));

    // We are the media source; downstream bridges accumulate their own latency.
    info.accumulated_latency = 0;
    return info;
}

//
// Event Handlers
//

void AvbEntityAm824IO::on_link_up(TimePoint time)
{
    supervisor_ctx_.link_up = true;
    supervisor_.handle_event(supervisor_ctx_, nanoavb::supervisor_sm::Def::Event::LinkUp, time);
}

void AvbEntityAm824IO::on_link_down(TimePoint time)
{
    supervisor_ctx_.link_up = false;
    supervisor_.handle_event(supervisor_ctx_, nanoavb::supervisor_sm::Def::Event::LinkDown, time);
}

void AvbEntityAm824IO::on_gptp_announce(TimePoint time, bool has_grandmaster)
{
    if (has_grandmaster && !gptp_ctx_.time_locked) {
        gptp_.handle_event(gptp_ctx_, nanoavb::gptp_sm::Def::Event::LockedStable, time);
    }
}

void AvbEntityAm824IO::on_timeout(TimePoint time)
{
    using nanoavb::supervisor_sm::Def;
    auto const state = supervisor_.current_state();

    // Only Init has a watchdog now (gPTP-lock timeout). There is no VLAN gate.
    if (state == Def::State::Init) {
        supervisor_.handle_event(supervisor_ctx_, Def::Event::Timeout, time);
    }
}

//
// Audio Processing
//

void AvbEntityAm824IO::process_audio(TimePoint time)
{
    // Source: generate the per-channel sine into the interleaved buffer.
    for (size_t i = 0; i < SAMPLES_PER_PACKET; ++i) {
        for (size_t ch = 0; ch < channels_; ++ch) {
            audio_buffer_[(i * channels_) + ch] = oscillators_[ch](0.0F);
        }
    }

    // Process through per-channel biquad filters (interleaved layout).
    for (size_t i = 0; i < SAMPLES_PER_PACKET; ++i) {
        for (size_t ch = 0; ch < channels_; ++ch) {
            audio_buffer_[(i * channels_) + ch] = biquads_[ch](audio_buffer_[(i * channels_) + ch]);
        }
    }

    // Call custom audio callback if set.
    if (audio_callback_) {
        audio_callback_(std::span{audio_buffer_}, SAMPLES_PER_PACKET);
    }

    // Transmit: stream continuously to the talker's multicast destination.
    // process_audio() only runs when the media timer fires, which (with the
    // linuxptp driver) only happens while the PTP bridge is healthy — i.e. we
    // have a usable clock. We deliberately do NOT gate on the announce-based
    // supervisor lock (talker_engine_ctx_.send_allowed): loss of gPTP Announces
    // must not stop transmission. A proper "stop after prolonged sync loss"
    // gate (drift-budget / asCapable model) is future work — see the
    // gptp-sync-loss-drift-budget design note.
    if (!talker_out_ || stream_tx_.fd() < 0) {
        return;
    }

    auto const now_ns = static_cast<uint64_t>(time.time_since_epoch().count());

    static constexpr size_t MAX_FRAME =
        avtp::Am824Pdu::HEADER_LENGTH + (avtp::Am824Pdu::MAX_SAMPLES_PER_PACKET * avtp::Am824Pdu::MAX_CHANNELS * 4);
    std::array<uint8_t, MAX_FRAME> frame{};

    avtp::Am824Pdu pdu{};
    pdu.init(talker_out_->stream_id, static_cast<uint8_t>(channels_), avtp::Am824SampleRate::rate_96_khz);

    std::span<uint8_t> const payload = std::span<uint8_t>{frame}.subspan(avtp::Am824Pdu::HEADER_LENGTH);
    size_t const audio_bytes = avtp::am824_serialize_mbla(
        *talker_out_, pdu, payload, static_cast<uint8_t>(SAMPLES_PER_PACKET), now_ns, [this](uint8_t ch, std::span<float> dest) {
            for (size_t s = 0; s < dest.size(); ++s) {
                dest[s] = audio_buffer_[(s * channels_) + ch];
            }
        });
    if (audio_bytes == 0) {
        return;
    }

    // Prepend the 32-byte AM824 header (am824_serialize_mbla filled `pdu`).
    span_store(std::span<uint8_t>{frame}.first(avtp::Am824Pdu::HEADER_LENGTH), pdu);
    size_t const frame_len = avtp::Am824Pdu::HEADER_LENGTH + audio_bytes;
    (void)stream_tx_.send(&stream_dest_mac_, std::span<uint8_t const>{frame.data(), frame_len});
    ++stream_tx_packets_;
}

void AvbEntityAm824IO::on_stream_rx_frame(std::span<uint8_t const> frame, int64_t now_ns)
{
    if (!listener_in_ || frame.size() < avtp::Am824Pdu::HEADER_LENGTH) {
        return;
    }
    if (frame[0] != avtp::AvtpSubtype::iec_61883_iidc) {
        return;  // not an IEC 61883/AM824 stream frame
    }

    avtp::Am824Pdu pdu{};
    span_load(pdu, frame.first(avtp::Am824Pdu::HEADER_LENGTH));
    if (!pdu.is_valid()) {
        stream_rx_bad_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    std::span<uint8_t const> const audio = frame.subspan(avtp::Am824Pdu::HEADER_LENGTH);
    uint64_t samples_this = 0;
    avtp::am824_deserialize_mbla(
        *listener_in_,
        pdu,
        audio,
        static_cast<uint64_t>(now_ns),
        [&samples_this](uint8_t /*ch*/, std::span<float> s, uint64_t /*pts*/, uint64_t /*period*/) {
            samples_this = s.size();  // identical across channels
        });

    stream_rx_packets_.fetch_add(1, std::memory_order_relaxed);
    stream_rx_samples_.fetch_add(samples_this, std::memory_order_relaxed);
}

void AvbEntityAm824IO::configure_filter(double const freq_hz, double const gain_db, double const q)
{
    double const sample_rate_recip = 1.0 / static_cast<double>(SAMPLE_RATE);

    // Apply the same peak EQ coefficients to all per-channel biquad filters
    for (size_t ch = 0; ch < channels_; ++ch) {
        biquads_[ch].coeffs.calculate_peak(
            {.sample_rate_recip = sample_rate_recip, .frequency = freq_hz, .q = q, .gain_db = gain_db, .channel = 0});
        biquads_[ch].reset();
    }
}

}  // namespace statusbar::avb_entity
