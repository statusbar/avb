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

/// Build NanoAvbComponents from an EntityModel and config settings.
/// The entity_model parameter must be the same object that lives in NanoAvbComponents
/// (i.e., this function is called just before aggregate-initialization of the struct).
static auto build_components(EntityModel& entity_model, AvbEntityAm824IOConfig const& config) -> NanoAvbComponents
{
    auto const& entity = entity_model.get_entity();

    // Create AEM command handler (references entity_model inside NanoAvbComponents)
    AemCommandHandler aem_handler{entity_model};

    // Create ADP advertiser
    AdpAdvertiserCallbacks const adp_callbacks{};
    AdpAdvertiserConfig adp_config{};
    adp_config.valid_time = 31;                                        // 62 seconds
    adp_config.reannounce_interval = std::chrono::milliseconds{5000};  // 5 seconds
    NanoAvbAdpAdvertiser adp_advertiser{entity_model.get_entity(), adp_callbacks, adp_config};

    // Create ACMP talker with one stream and 4 max connections
    AcmpTalkerCallbacks const talker_callbacks{};
    NanoAvbAcmpTalker acmp_talker{entity.entity_id, talker_callbacks, 1, 4};

    // Configure talker stream with multicast MAC from config
    ieee::Eui64 stream_id{};
    span_copy(stream_id.span().first(6), entity.entity_id.span().first(6));
    (void)acmp_talker.configure_stream(0, stream_id, config.talker_dest_mac);

    // Create ACMP listener with one stream
    AcmpListenerCallbacks const listener_callbacks{};
    NanoAvbAcmpListener acmp_listener{entity.entity_id, listener_callbacks, 1};

    // Create MVRP handler
    MvrpCallbacks const mvrp_callbacks{};
    MvrpHandler mvrp_handler{statusbar::srp::mvrp::MvrpConfig{}, mvrp_callbacks};
    (void)mvrp_handler.register_vlan(config.vlan_id, sm::Clock::now());

    // Create MSRP handler
    MsrpCallbacks const msrp_callbacks{};
    MsrpHandler msrp_handler{statusbar::srp::msrp::MsrpConfig{}, msrp_callbacks};
    msrp_handler.set_domain(
        DomainInfo{
            .sr_class_id = 6,        // SR Class A
            .sr_class_priority = 3,  // Priority 3
            .sr_class_vid = config.vlan_id});

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

    // Step 3: Build NanoAvbComponents from the entity model and config
    auto components = build_components(*model_result, config);

    // Step 4: Construct entity via unique_ptr (private constructor)
    std::pmr::memory_resource* const mr = memory_resource != nullptr ? memory_resource : std::pmr::get_default_resource();
    std::unique_ptr<AvbEntityAm824IO> entity{new AvbEntityAm824IO{std::move(config), std::move(components), channels, mr}};

    // Step 5: Configure filter and set talker stream ID
    entity->configure_filter(entity->config_.filter_freq_hz, entity->config_.filter_gain_db, entity->config_.filter_q);

    entity->talker_stream_id_ = entity->config_.entity_id;

    return success(std::move(entity));
}

//
// Constructor / Destructor
//

AvbEntityAm824IO::AvbEntityAm824IO(
    AvbEntityAm824IOConfig config,
    nanoavb::NanoAvbComponents components,
    size_t channels,
    std::pmr::memory_resource* memory_resource)
    : config_{std::move(config)}
    , components_{std::move(components)}
    , channels_{channels}
    , mem_resource_{memory_resource}
    , biquads_(channels, dsp::BiQuad<float>{}, mem_resource_)
    , audio_buffer_(SAMPLES_PER_PACKET * channels, 0.0f, mem_resource_)
{}

AvbEntityAm824IO::~AvbEntityAm824IO()
{
    try {
        if (running_) {
            (void)stop();
        }
    } catch (...) {  // NOLINT(bugprone-empty-catch) - destructors must not throw
    }
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

auto AvbEntityAm824IO::start(net::MessageReactor& reactor) -> Status
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

auto AvbEntityAm824IO::state_string() const -> std::string
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

void AvbEntityAm824IO::print_state() const
{
    std::print(
        "State: supervisor={} gptp={} mvrp={} channels={}\n",
        state_string(),
        gptp_ctx_.time_locked ? "Locked" : "Unlocked",
        mvrp_ctx_.joined ? "Joined" : "NotJoined",
        channels_);
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

    if (state == Def::State::Init || state == Def::State::WaitVlanBase) {
        supervisor_.handle_event(supervisor_ctx_, Def::Event::Timeout, time);
    }
}

//
// Audio Processing
//

void AvbEntityAm824IO::process_audio(TimePoint time)
{
    (void)time;

    // Process through per-channel biquad filters (interleaved layout)
    for (size_t i = 0; i < SAMPLES_PER_PACKET; ++i) {
        for (size_t ch = 0; ch < channels_; ++ch) {
            audio_buffer_[(i * channels_) + ch] = biquads_[ch](audio_buffer_[(i * channels_) + ch]);
        }
    }

    // Call custom audio callback if set
    if (audio_callback_) {
        audio_callback_(std::span{audio_buffer_}, SAMPLES_PER_PACKET);
    }
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
