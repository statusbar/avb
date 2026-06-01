// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// AVB Entity Audio I/O Implementation (dual-format AM824 + AAF)
/// Implements AvbEntityAudioIO: blob-loaded model, shared N-channel DSP source,
/// one AM824 talker/listener (stream 0) and one AAF talker/listener (stream 1).

#include "statusbar/avb_entity/avb_entity_audio_io.hpp"

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
#include "statusbar/net/net_address.hpp"
#include "statusbar/net/net_message_reactor.hpp"
#include "statusbar/net/net_posix_util.hpp"
#include "statusbar/net/net_socket.hpp"
#include "statusbar/net/net_util.hpp"
#include "statusbar/sm/sm.hpp"
#include "statusbar/status/catch_or_status.hpp"
#include "statusbar/status/status.hpp"
#include "statusbar/udptun/udptun_aaf_v1_codec.hpp"
#include "statusbar/udptun/udptun_audio_ingest.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <expected>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <sys/socket.h>

namespace statusbar::avb_entity {

using namespace statusbar::atdecc;
using namespace statusbar::atdecc::aem;
using namespace statusbar::nanoavb;

namespace {

/// Reactor port that receives AVTP stream frames on a dedicated socket joined to
/// BOTH stream multicast groups (AM824 + AAF), and hands each frame to the owning
/// entity for subtype dispatch / decode / metering.
class StreamRxHandler : public net::Pollable
{
  public:
    StreamRxHandler(
        std::string_view interface_name, ieee::Eui48 const& am824_group, ieee::Eui48 const& aaf_group, AvbEntityAudioIO* owner)
        : owner_{owner}
    {
        // Open joined to the AM824 group, then add the AAF group. The NIC's
        // multicast hash filter delivers both to this (non-promiscuous) socket.
        (void)sock_.open(interface_name, avtp::AVTP_ETHERTYPE, &am824_group, /*qdisc_bypass=*/false);
        (void)sock_.join_multicast(aaf_group);
    }

    [[nodiscard]] auto valid() const noexcept -> bool { return sock_.fd() >= 0; }

    /// Non-owning access to the RX socket, so the entity can join/leave a remote
    /// talker's stream multicast group at connect/disconnect time. Stable across
    /// the move into the reactor (the handler is heap-allocated; only the
    /// unique_ptr moves), so a pointer taken before the move stays valid.
    [[nodiscard]] auto socket() noexcept -> net::RawnetContext* { return &sock_; }

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
    AvbEntityAudioIO* owner_;
};

//
// File-static helpers
//

template <typename T>
auto load_descriptor(DescriptorStorage const& storage, uint16_t config_idx, uint16_t type, uint16_t index) -> StatusValue<T>
{
    auto result = storage.get_descriptor(config_idx, type, index);
    if (!result) {
        return failure(result.error());
    }
    T desc{};
    span_load_padded(desc, *result);
    return success(desc);
}

auto count_descriptors(DescriptorStorage const& storage, uint16_t config_idx, uint16_t type) -> size_t
{
    size_t count = 0;
    while (storage.get_descriptor(config_idx, type, static_cast<uint16_t>(count))) {
        ++count;
    }
    return count;
}

/// Build an EntityModel from the blob, overriding runtime fields from config and
/// reporting the channel count (from the first AUDIO_CLUSTER). Requires >=2
/// stream inputs and >=2 stream outputs (one AM824 + one AAF each way).
auto build_entity_model(DescriptorStorage const& storage, AvbEntityAudioIOConfig const& config, size_t& out_channels)
    -> StatusValue<EntityModel>
{
    uint16_t const config_idx = 0;

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

    if (n_stream_inputs < 2 || n_stream_outputs < 2 || n_avb_interfaces < 1) {
        return failure(BufferError::insufficient_data);
    }

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

    for (uint16_t i = 0; i < static_cast<uint16_t>(n_configurations); ++i) {
        if (auto r = load_descriptor<DescriptorConfiguration>(storage, config_idx, DESCRIPTOR_CONFIGURATION, i)) {
            (void)model.add_configuration(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_audio_units); ++i) {
        if (auto r = load_descriptor<DescriptorAudioUnit>(storage, config_idx, DESCRIPTOR_AUDIO_UNIT, i)) {
            (void)model.add_audio_unit(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_stream_inputs); ++i) {
        if (auto r = load_descriptor<DescriptorStream>(storage, config_idx, DESCRIPTOR_STREAM_INPUT, i)) {
            (void)model.add_stream_input(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_stream_outputs); ++i) {
        if (auto r = load_descriptor<DescriptorStream>(storage, config_idx, DESCRIPTOR_STREAM_OUTPUT, i)) {
            (void)model.add_stream_output(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_jack_inputs); ++i) {
        if (auto r = load_descriptor<DescriptorJack>(storage, config_idx, DESCRIPTOR_JACK_INPUT, i)) {
            (void)model.add_jack_input(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_jack_outputs); ++i) {
        if (auto r = load_descriptor<DescriptorJack>(storage, config_idx, DESCRIPTOR_JACK_OUTPUT, i)) {
            (void)model.add_jack_output(*r);
        }
    }
    // The blob leaves AVB_INTERFACE's network/gPTP identity zero; populate it at
    // load (like entity_id) so compliant controllers (Hive) accept the entity.
    // mac_address = the live NIC MAC; clock_identity = its modified EUI-64, which
    // is ptp4l's default gPTP clockIdentity; plus default gPTP port params so the
    // grandmaster/clock info isn't blank/invalid.
    auto const iface_mac = net::read_interface_mac(config.interface_name);
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_avb_interfaces); ++i) {
        if (auto r = load_descriptor<DescriptorAvbInterface>(storage, config_idx, DESCRIPTOR_AVB_INTERFACE, i)) {
            if (iface_mac) {
                r->mac_address = *iface_mac;
                r->clock_identity = iface_mac->to_modified_eui64();
            }
            r->priority1 = 248;    // gPTP default priority1
            r->clock_class = 248;  // not grandmaster-capable (slave-only)
            r->offset_scaled_log_variance = 0x436A;
            r->clock_accuracy = 0xFE;  // unknown
            r->priority2 = 248;
            r->domain_number = 0;
            r->log_sync_interval = static_cast<uint8_t>(static_cast<int8_t>(-3));  // 125 ms (gPTP Class A)
            r->log_announce_interval = 0;                                          // 1 s
            r->log_pdelay_interval = 0;                                            // 1 s
            (void)model.add_avb_interface(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_clock_sources); ++i) {
        if (auto r = load_descriptor<DescriptorClockSource>(storage, config_idx, DESCRIPTOR_CLOCK_SOURCE, i)) {
            (void)model.add_clock_source(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_clock_domains); ++i) {
        if (auto r = load_descriptor<DescriptorClockDomain>(storage, config_idx, DESCRIPTOR_CLOCK_DOMAIN, i)) {
            (void)model.add_clock_domain(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_locales); ++i) {
        if (auto r = load_descriptor<DescriptorLocale>(storage, config_idx, DESCRIPTOR_LOCALE, i)) {
            (void)model.add_locale(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_strings); ++i) {
        if (auto r = load_descriptor<DescriptorStrings>(storage, config_idx, DESCRIPTOR_STRINGS, i)) {
            (void)model.add_strings(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_stream_port_inputs); ++i) {
        if (auto r = load_descriptor<DescriptorStreamPort>(storage, config_idx, DESCRIPTOR_STREAM_PORT_INPUT, i)) {
            (void)model.add_stream_port_input(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_stream_port_outputs); ++i) {
        if (auto r = load_descriptor<DescriptorStreamPort>(storage, config_idx, DESCRIPTOR_STREAM_PORT_OUTPUT, i)) {
            (void)model.add_stream_port_output(*r);
        }
    }

    out_channels = 2;
    if (auto r = load_descriptor<DescriptorAudioCluster>(storage, config_idx, DESCRIPTOR_AUDIO_CLUSTER, 0)) {
        auto const ch = static_cast<uint16_t>(r->channel_count);
        if (ch >= 1) {
            out_channels = static_cast<size_t>(ch);
        }
    }

    for (uint16_t i = 0; i < static_cast<uint16_t>(n_audio_clusters); ++i) {
        if (auto r = load_descriptor<DescriptorAudioCluster>(storage, config_idx, DESCRIPTOR_AUDIO_CLUSTER, i)) {
            (void)model.add_audio_cluster(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_audio_maps); ++i) {
        if (auto r = load_descriptor<DescriptorAudioMap>(storage, config_idx, DESCRIPTOR_AUDIO_MAP, i)) {
            (void)model.add_audio_map(*r);
        }
    }
    for (uint16_t i = 0; i < static_cast<uint16_t>(n_controls); ++i) {
        if (auto r = load_descriptor<DescriptorControl>(storage, config_idx, DESCRIPTOR_CONTROL, i)) {
            (void)model.add_control(*r);
        }
    }

    return success(std::move(model));
}

auto make_adp_config() -> AdpAdvertiserConfig
{
    AdpAdvertiserConfig adp_config{};
    adp_config.valid_time = 31;                                        // 62 seconds
    adp_config.reannounce_interval = std::chrono::milliseconds{5000};  // 5 seconds
    return adp_config;
}

/// Derive a stream id from the entity id, with the low byte set to the stream
/// index so the two talker streams have distinct ids.
// Derive a globally-unique IEEE 1722 stream_id from the talker's NIC MAC (high 6
// bytes, unique per interface) plus a per-stream unique_id in the low 16 bits.
// Basing it on the MAC -- not the entity_id -- guarantees two entities never get
// colliding stream_ids even when their entity_ids share their high 48 bits (which
// otherwise silently broke RX stream demux + MSRP reservations: two AAF streams
// with the same stream_id are indistinguishable to a listener).
auto stream_id_for(ieee::Eui48 const& base_mac, uint16_t index) -> ieee::Eui64
{
    ieee::Eui64 sid{};
    auto const mac_bytes = make_const_span(base_mac);
    span_copy(sid.span().first(6), mac_bytes.first(6));
    sid.span()[6] = 0;
    sid.span()[7] = static_cast<uint8_t>(index & 0xFFU);
    return sid;
}

}  // namespace

//
// Factory method
//

auto AvbEntityAudioIO::create(AvbEntityAudioIOConfig config, std::pmr::memory_resource* memory_resource)
    -> StatusValue<std::unique_ptr<AvbEntityAudioIO>>
{
    auto storage_result = DescriptorStorage::create(std::span<uint8_t const>{config.descriptor_storage_blob});
    if (!storage_result) {
        return failure(storage_result.error());
    }

    size_t channels = 2;
    auto model_result = build_entity_model(*storage_result, config, channels);
    if (!model_result) {
        return failure(model_result.error());
    }

    std::pmr::memory_resource* const mr = memory_resource != nullptr ? memory_resource : std::pmr::get_default_resource();
    auto entity = std::make_unique<AvbEntityAudioIO>(
        AvbEntityAudioIO::CreateKey{}, std::move(config), std::move(*model_result), channels, mr);

    entity->configure_filter(entity->config_.filter_freq_hz, entity->config_.filter_gain_db, entity->config_.filter_q);

    return success(std::move(entity));
}

//
// Constructor / Destructor
//

AvbEntityAudioIO::AvbEntityAudioIO(
    CreateKey,
    AvbEntityAudioIOConfig config,
    nanoavb::EntityModel entity_model,
    size_t channels,
    std::pmr::memory_resource* memory_resource)
    : config_{std::move(config)}  // Two talker streams (4 max listeners each), two listener streams.
    , components_{std::move(entity_model), make_adp_config(), 3, 4, 2}  // 3 talkers: AM824, AAF, CRF
    , channels_{channels}
    , mem_resource_{memory_resource}
    , biquads_(channels, dsp::BiQuad<float>{}, mem_resource_)
    , audio_buffer_((SAMPLES_PER_PACKET + 1) * channels, 0.0f, mem_resource_)  // +1: GPS pacing may emit nominal+1
    , oscillators_(channels, dsp::Oscillator<float>{}, mem_resource_)
    // AAF reframe FIFO: a wake pushes <= SAMPLES_PER_PACKET+1 frames and then
    // drains 12-blocks, so the backlog never exceeds ~2 blocks. 4 blocks of
    // headroom keeps a fixed, allocation-free buffer.
    , aaf_reframer_(channels, SAMPLES_PER_PACKET, 4, mem_resource_)
{
    // Per-channel sine source: identical frequency, distinct initial phase.
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

    // ATDECC descriptor wire version. Default "2016": emit descriptors at their
    // 1722.1-2013/2016 lengths so controllers that reject 2021-length descriptors
    // (Hive/Compass) can enumerate the model. "2021" emits the full 2021 forms.
    components_.aem_handler.set_legacy_2016(config_.atdecc_version != "2021");

    auto const& entity = components_.entity_model.get_entity();

    // Base the talker stream_ids on the NIC MAC (globally unique per box). Fall
    // back to the entity_id's high 6 bytes only when the interface MAC can't be
    // read (e.g. unit tests with a dummy interface).
    ieee::Eui48 stream_base_mac{};
    if (auto const mac = net::read_interface_mac(config_.interface_name)) {
        stream_base_mac = *mac;
    } else {
        span_copy(stream_base_mac.span(), entity.entity_id.span().first(6));
    }

    // Configure talker stream 0 (AM824) and stream 1 (AAF) with distinct stream
    // ids and destination multicast MACs, so MSRP/ACMP/AVTP agree per stream.
    (void)components_.acmp_talker.configure_stream(
        AM824_STREAM_INDEX, stream_id_for(stream_base_mac, AM824_STREAM_INDEX), config_.am824_talker_dest_mac);
    (void)components_.acmp_talker.configure_stream(
        AAF_STREAM_INDEX, stream_id_for(stream_base_mac, AAF_STREAM_INDEX), config_.aaf_talker_dest_mac);
    (void)components_.acmp_talker.configure_stream(
        CRF_STREAM_INDEX, stream_id_for(stream_base_mac, CRF_STREAM_INDEX), config_.crf_talker_dest_mac);

    (void)components_.mvrp_handler.register_vlan(config_.vlan_id, sm::Clock::now());
    components_.msrp_handler.set_domain(DomainInfo{.sr_class_id = 6, .sr_class_priority = 3, .sr_class_vid = config_.vlan_id});
    components_.msrp_handler.set_redeclare_registered_listeners(config_.redeclare_registered_listeners);
    components_.msrp_handler.set_suppress_leaveall(config_.suppress_leaveall);
}

AvbEntityAudioIO::~AvbEntityAudioIO()
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
// State machine callback wiring (identical lifecycle to the AM824 entity)
//

void AvbEntityAudioIO::wire_callbacks()
{
    supervisor_ctx_.callbacks.init_iface = [this](auto& /*ctx*/, TimePoint /*time*/) -> void {
        gptp_.reset();
        mvrp_.reset();
    };

    supervisor_ctx_.callbacks.start_protocols = [this](auto& /*ctx*/, TimePoint time) -> void {
        gptp_.handle_event(gptp_ctx_, nanoavb::gptp_sm::Def::Event::AsCapableUp, time);
        auto result = components_.adp_advertiser.start(time);
        if (!result) {
            std::print(stderr, "Warning: ADP start failed: {}\n", result.error().message());
        }
    };

    supervisor_ctx_.callbacks.enter_ready = [this](auto& /*ctx*/, TimePoint time) -> void {
        mvrp_.handle_event(mvrp_ctx_, nanoavb::mvrp_sm::Def::Event::Acquire, time);
        msrp_talker_.handle_event(msrp_talker_ctx_, nanoavb::msrp_talker_sm::Def::Event::StartAdvertise, time);
        talker_engine_ctx_.send_allowed = true;
        listener_engine_ctx_.play_allowed = true;
    };

    supervisor_ctx_.callbacks.degrade_stop_streams = [this](auto& /*ctx*/, TimePoint time) -> void {
        talker_engine_.handle_event(talker_engine_ctx_, nanoavb::talker_engine_sm::Def::Event::GateStop, time);
        listener_engine_.handle_event(listener_engine_ctx_, nanoavb::listener_engine_sm::Def::Event::GateStop, time);
        talker_engine_ctx_.send_allowed = false;
        listener_engine_ctx_.play_allowed = false;
        msrp_talker_.handle_event(msrp_talker_ctx_, nanoavb::msrp_talker_sm::Def::Event::StopAdvertise, time);
        mvrp_.reset();
        msrp_talker_.reset();
        msrp_listener_.reset();
    };

    supervisor_ctx_.callbacks.stop_all = [this](auto& /*ctx*/, TimePoint time) -> void {
        (void)components_.adp_advertiser.stop(time);
        talker_engine_.handle_event(talker_engine_ctx_, nanoavb::talker_engine_sm::Def::Event::Fatal, time);
        listener_engine_.handle_event(listener_engine_ctx_, nanoavb::listener_engine_sm::Def::Event::GateStop, time);
        gptp_.reset();
        mvrp_.reset();
        msrp_talker_.reset();
        msrp_listener_.reset();
        talker_engine_ctx_.send_allowed = false;
        listener_engine_ctx_.play_allowed = false;
    };

    supervisor_ctx_.callbacks.timeout_gptp = [](auto& /*ctx*/, TimePoint /*time*/) -> void {
        std::print(stderr, "[supervisor] gPTP lock timeout\n");
    };

    gptp_ctx_.callbacks.init = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    gptp_ctx_.callbacks.start_servo = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    gptp_ctx_.callbacks.report_locked = [this](auto& /*ctx*/, TimePoint time) -> void {
        supervisor_.handle_event(supervisor_ctx_, nanoavb::supervisor_sm::Def::Event::GptpLocked, time);
    };
    gptp_ctx_.callbacks.report_unlocked = [this](auto& /*ctx*/, TimePoint time) -> void {
        supervisor_.handle_event(supervisor_ctx_, nanoavb::supervisor_sm::Def::Event::GptpLost, time);
    };

    mvrp_ctx_.callbacks.init = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    mvrp_ctx_.callbacks.send_join = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    mvrp_ctx_.callbacks.mark_joined = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    mvrp_ctx_.callbacks.mark_error = [](auto& /*ctx*/, TimePoint /*time*/) -> void {
        std::print(stderr, "[mvrp] VLAN registration failed\n");
    };
    mvrp_ctx_.callbacks.send_leave = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    mvrp_ctx_.callbacks.mark_left = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    mvrp_ctx_.callbacks.reset = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};

    msrp_talker_ctx_.callbacks.init = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    msrp_talker_ctx_.callbacks.msrp_talker_advertise = [this](auto& /*ctx*/, TimePoint time) -> void {
        // Declare our SR class domain(s) before advertising streams so the bridge
        // includes this port in the SRP domain; otherwise inbound Talker Advertise
        // declarations are converted to Talker Failed (failure_code 8).
        (void)components_.msrp_handler.declare_domain(time);
        // Advertise BOTH talker streams (AM824 stream 0 + AAF stream 1) so a remote
        // listener can reserve either; advertising only one left the other stream
        // unreserved and the bridge never forwarded it (listener saw FRAMES_RX=0).
        for (uint16_t const idx : {AM824_STREAM_INDEX, AAF_STREAM_INDEX, CRF_STREAM_INDEX}) {
            auto result = components_.msrp_handler.talker_advertise(make_talker_srp_info(idx), time);
            if (!result) {
                std::print(stderr, "Warning: MSRP talker_advertise (stream {}) failed: {}\n", idx, result.error().message());
            }
        }
    };
    msrp_talker_ctx_.callbacks.mark_ready = [this](auto& /*ctx*/, TimePoint time) -> void {
        if (supervisor_.current_state() == nanoavb::supervisor_sm::Def::State::Ready) {
            talker_engine_.handle_event(talker_engine_ctx_, nanoavb::talker_engine_sm::Def::Event::AudioReady, time);
        }
    };
    msrp_talker_ctx_.callbacks.mark_failed = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    msrp_talker_ctx_.callbacks.msrp_talker_withdraw = [this](auto& /*ctx*/, TimePoint time) -> void {
        for (uint16_t const idx : {AM824_STREAM_INDEX, AAF_STREAM_INDEX, CRF_STREAM_INDEX}) {
            (void)components_.msrp_handler.talker_withdraw(make_talker_srp_info(idx).stream_id, time);
        }
    };
    msrp_talker_ctx_.callbacks.mark_idle = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};

    components_.msrp_handler.set_on_talker_listener([this](nanoavb::StreamId const& stream_id, bool ready) {
        auto const now = sm::Clock::now();
        msrp_talker_.handle_event(
            msrp_talker_ctx_, ready ? nanoavb::msrp_talker_sm::Def::Event::Ready : nanoavb::msrp_talker_sm::Def::Event::Lost, now);

        // Diagnostic: log the EXACT reason behind every gate decision (fires on
        // each listener register/leave for one of our talker streams), so the
        // listener-ready flap is explained -- which of the four conditions
        // (record present, operation==Register, registrar In, Ready substate)
        // is flipping. `permits` == `ready`.
        {
            auto const dbg = components_.msrp_handler.listener_permit_debug(stream_id);
            std::print(
                "[srp-gate] sid={:016x} permits={} | record={} op={} registrar_in={} substate={}\n",
                stream_id.to_uint64(),
                dbg.permits,
                dbg.has_record,
                dbg.operation == statusbar::srp::msrp::Operation::Register ? "Register" : "Declare",
                dbg.registrar_in,
                statusbar::srp::msrp::listener_declaration_name(dbg.substate));
        }

        // Per-stream transmit gate: record whether this talker stream now has a
        // listener that permits transmit (MSRP Listener Ready). Matched to the
        // stream index by the ACMP stream_id (same value MSRP advertises).
        uint64_t const sid_u64 = stream_id.to_uint64();
        for (uint16_t idx = 0; idx < static_cast<uint16_t>(msrp_listener_ready_.size()); ++idx) {
            auto const* s = components_.acmp_talker.get_stream(idx);
            if (s != nullptr && s->stream_id.to_uint64() == sid_u64) {
                bool const was = msrp_listener_ready_[idx].exchange(ready, std::memory_order_relaxed);
                if (ready) {
                    // Stamp the last-ready time so the strict gate's grace window
                    // survives the next LeaveAll re-registration blip.
                    msrp_ready_ns_[idx].store(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                            .count(),
                        std::memory_order_relaxed);
                }
                if (was != ready) {
                    std::print("[srp] talker stream {} MSRP listener-ready -> {} (ACMP-AND-MSRP gate)\n", idx, ready);
                }
            }
        }
    });

    components_.acmp_talker.set_connection_callbacks(
        [](uint16_t stream_index, ieee::Eui64 listener_entity_id, uint16_t listener_unique_id) {
            std::print(
                "[acmp] talker stream {} ({}) CONNECTED by listener {:012x} unique_id {}\n",
                stream_index,
                stream_index == AAF_STREAM_INDEX ? "AAF" : "AM824",
                listener_entity_id.to_uint64(),
                listener_unique_id);
        },
        [](uint16_t stream_index, ieee::Eui64 listener_entity_id, uint16_t listener_unique_id) {
            std::print(
                "[acmp] talker stream {} ({}) DISCONNECTED by listener {:012x} unique_id {}\n",
                stream_index,
                stream_index == AAF_STREAM_INDEX ? "AAF" : "AM824",
                listener_entity_id.to_uint64(),
                listener_unique_id);
        });

    // Our listener connected to / disconnected from a remote talker. On connect,
    // reserve the talker's stream via MSRP (Listener Ready) so the talker actually
    // starts streaming -- many talkers (Milan, the DSP processor) gate streaming on the SRP
    // reservation rather than on ACMP alone. MSRP/MVRP are peer-to-peer MRP
    // applications: the Listener Ready declaration is registered by the adjacent
    // switch port and re-propagated hop-by-hop until it reaches the talker's port.
    //
    // We also join the talker's stream multicast group (`dest_mac`) on the RX
    // socket here, and leave it on disconnect. The StreamRxHandler only joins our
    // own talker groups (fe:00/fe:01) at startup, so without this a remote
    // talker's frames are dropped by the NIC filter. The the DSP processor allocates a
    // NEW dest MAC every connection (e.g. 91:e0:f0:00:9a:60 then ...9a:61), so the
    // group must be learned from the ACMP response each time -- not preconfigured.
    // This makes listener reception need nothing but ACMP CONNECT_RX (no manual
    // `ip maddr add`). See avb/docs/GPS_MEDIA_CLOCK.md.
    components_.acmp_listener.set_connection_callbacks(
        [this](uint16_t stream_index, ieee::Eui64 const& stream_id, ieee::Eui48 dest_mac) {
            tsn::StreamId sid{};
            (void)statusbar::tsn::load_unchecked(stream_id.span(), &sid);
            auto const now = sm::Clock::now();
            auto const result = components_.msrp_handler.listener_ready(sid, now);
            // Join the talker's stream group so the NIC delivers its frames to us.
            bool const joined = rx_sock_ != nullptr && rx_sock_->join_multicast(dest_mac).has_value();
            std::print(
                "[acmp] listener stream {} ({}) CONNECTED to talker dest={:012x} -> MSRP Listener Ready {}, mcast join {}\n",
                stream_index,
                stream_index == AAF_STREAM_INDEX ? "AAF" : "AM824",
                dest_mac.to_uint64(),
                result.has_value() ? "declared" : "failed",
                joined ? "ok" : "FAILED");
        },
        [this](uint16_t stream_index) {
            if (auto const* stream = components_.acmp_listener.get_stream(stream_index); stream != nullptr) {
                tsn::StreamId sid{};
                (void)statusbar::tsn::load_unchecked(stream->stream_id.span(), &sid);
                (void)components_.msrp_handler.listener_withdraw(sid, sm::Clock::now());
                // Release the talker's stream group so per-connection dest-MAC
                // churn does not pile up stale NIC multicast filter entries.
                if (rx_sock_ != nullptr) {
                    (void)rx_sock_->leave_multicast(stream->stream_dest_mac);
                }
            }
            std::print(
                "[acmp] listener stream {} ({}) DISCONNECTED from talker -> MSRP Listener withdrawn\n",
                stream_index,
                stream_index == AAF_STREAM_INDEX ? "AAF" : "AM824");
        });

    msrp_listener_ctx_.callbacks.init = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    msrp_listener_ctx_.callbacks.msrp_listener_ready = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    msrp_listener_ctx_.callbacks.mark_ready = [this](auto& /*ctx*/, TimePoint time) -> void {
        if (supervisor_.current_state() == nanoavb::supervisor_sm::Def::State::Ready) {
            listener_engine_.handle_event(listener_engine_ctx_, nanoavb::listener_engine_sm::Def::Event::GateListen, time);
        }
    };
    msrp_listener_ctx_.callbacks.mark_failed = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    msrp_listener_ctx_.callbacks.msrp_listener_leave = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    msrp_listener_ctx_.callbacks.mark_idle = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};

    talker_engine_ctx_.callbacks.init = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    talker_engine_ctx_.callbacks.start_audio_source = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    talker_engine_ctx_.callbacks.arm_stream = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    talker_engine_ctx_.callbacks.start_tx = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    talker_engine_ctx_.callbacks.stop_tx = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    talker_engine_ctx_.callbacks.mute_tx = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    talker_engine_ctx_.callbacks.unmute_tx = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    talker_engine_ctx_.callbacks.stop_all = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};

    listener_engine_ctx_.callbacks.init = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    listener_engine_ctx_.callbacks.enable_rx_filter = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    listener_engine_ctx_.callbacks.start_sync = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    listener_engine_ctx_.callbacks.start_audio_sink = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    listener_engine_ctx_.callbacks.stop_all = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    listener_engine_ctx_.callbacks.resync = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    listener_engine_ctx_.callbacks.mute_out = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
    listener_engine_ctx_.callbacks.unmute_out = [](auto& /*ctx*/, TimePoint /*time*/) -> void {};
}

//
// Start / Stop
//

auto AvbEntityAudioIO::start(net::MessageReactor& reactor) -> Status
{
    if (running_) {
        return failure(std::make_error_code(std::errc::already_connected));
    }

    net_handlers_ = std::make_unique<NanoAvbNetHandlers>(config_.interface_name, components_);
    net_handlers_->add_to_reactor(reactor);
    net_handlers_->print_warnings(config_.interface_name);

    setup_nanoavb_callbacks(components_, *net_handlers_);
    wire_callbacks();

    // Serve AECP GET_COUNTERS for our STREAM_INPUT (listener) and STREAM_OUTPUT
    // (talker) descriptors from the live stream counters, so a controller (or our
    // own statusbar-aem-get-counters) can read both our listener-side stream
    // health AND our talker-side frame rate -- e.g. to tell whether a low rate at
    // a remote listener is us under-transmitting or the switch under-forwarding.
    components_.aem_handler.set_get_counters(
        [this](uint16_t descriptor_type, uint16_t descriptor_index, uint32_t& valid, std::array<uint32_t, 32>& counters) -> bool {
            if (descriptor_type == DESCRIPTOR_STREAM_INPUT) {
                return fill_stream_input_counters(descriptor_index, valid, counters);
            }
            if (descriptor_type == DESCRIPTOR_STREAM_OUTPUT) {
                return fill_stream_output_counters(descriptor_index, valid, counters);
            }
            return false;
        });

    // Serve AECP GET_STREAM_INFO for our STREAM_OUTPUT (talker) descriptors. A
    // Milan listener (the DSP processor) queries this after connecting to verify the
    // talker's stream_id/format/dest/VLAN, and tears the connection down if the
    // talker answers NOT_IMPLEMENTED — which is why the earlier FAST_CONNECT to
    // the the DSP processor flapped. See avb/docs/GPS_MEDIA_CLOCK.md.
    components_.aem_handler.set_get_stream_info(
        [this](uint16_t descriptor_type, uint16_t descriptor_index, atdecc::aem::AemStreamInfoPayload& out) -> bool {
            return fill_stream_output_info(descriptor_type, descriptor_index, out);
        });

    // --- Stream data plane ---
    // Resolve both talker stream identities (id + dest MAC) from ACMP.
    statusbar::tsn::StreamId am824_sid{};
    if (auto const* s = components_.acmp_talker.get_stream(AM824_STREAM_INDEX); s != nullptr) {
        (void)statusbar::tsn::load_unchecked(s->stream_id.span(), &am824_sid);
        am824_dest_mac_ = s->stream_dest_mac;
    }
    statusbar::tsn::StreamId aaf_sid{};
    if (auto const* s = components_.acmp_talker.get_stream(AAF_STREAM_INDEX); s != nullptr) {
        (void)statusbar::tsn::load_unchecked(s->stream_id.span(), &aaf_sid);
        aaf_dest_mac_ = s->stream_dest_mac;
    }
    statusbar::tsn::StreamId crf_sid{};
    if (auto const* s = components_.acmp_talker.get_stream(CRF_STREAM_INDEX); s != nullptr) {
        (void)statusbar::tsn::load_unchecked(s->stream_id.span(), &crf_sid);
        crf_dest_mac_ = s->stream_dest_mac;
    }

    // The deterministic media clock owns the presentation offset and supplies the
    // avtp_timestamp, so the stream-output contexts add ZERO extra offset -- the
    // timestamp we pass them is already the final presentation time.
    media_clock_ = ptpclient::MediaClockGenerator{ptpclient::MediaClockGenerator::Config{
        .sample_rate_hz = static_cast<double>(SAMPLE_RATE), .presentation_offset_ns = config_.presentation_offset_ns}};
    gps_ratio_ = ptpclient::KalmanRatioTracker{ptpclient::KalmanRatioTracker::Config{.meas_noise_ns = 1000.0, .jerk_psd = 1e-3}};

    am824_out_.emplace(
        am824_sid, avtp::Am824SampleRate::rate_96_khz, static_cast<uint8_t>(channels_), /*presentation_offset_ns=*/0);
    am824_in_.emplace(avtp::Am824SampleRate::rate_96_khz, static_cast<uint8_t>(channels_));
    aaf_out_.emplace(
        aaf_sid, AAF_FORMAT, AAF_SAMPLE_RATE, static_cast<uint16_t>(channels_), AAF_BIT_DEPTH, /*presentation_offset_ns=*/0);
    aaf_in_.emplace(AAF_FORMAT, AAF_SAMPLE_RATE, static_cast<uint16_t>(channels_), AAF_BIT_DEPTH);
    // CRF media-clock talker: Milan 48 kHz audio-sample reference, pull x1.0. The
    // declared base is 48 kHz (CRF_BASE_FREQUENCY) so both 48 kHz and 96 kHz Milan
    // clients lock to it; our 96 kHz audio rides as a 2x multiple of this base.
    crf_out_.emplace(
        crf_sid,
        avtp::CrfType::audio_sample,
        CRF_BASE_FREQUENCY,
        avtp::CrfPull::multiply_1_0,
        config_.crf_timestamp_interval,
        config_.crf_timestamps_per_packet);
    crf_event_ = 0;

    // One TX socket (qdisc-bypass so our own egress is not re-received here).
    (void)stream_tx_.open(config_.interface_name, avtp::AVTP_ETHERTYPE, nullptr, /*qdisc_bypass=*/true);

    // Optional TX stream capture: because the socket is qdisc-bypass its egress is
    // invisible to any local capture, so tap it at the socket and record our own
    // transmitted frames (gPTP-timestamped) to a pcap for offline inspection.
    if (!config_.tx_pcap_path.empty()) {
        tx_pcap_recorder_.configure(
            config_.tx_pcap_path,
            config_.tx_pcap_max_bytes,
            /*snaplen=*/1522,
            static_cast<uint64_t>(config_.tx_pcap_seconds) * 1'000'000'000ULL);
        stream_tx_.set_tx_tap([this](std::span<uint8_t const> frame) { tx_pcap_recorder_.record(frame, last_tx_gptp_ns_); });
    }

    // One RX port joined to both stream groups; dispatch by subtype.
    auto rx = std::make_unique<StreamRxHandler>(config_.interface_name, am824_dest_mac_, aaf_dest_mac_, this);
    if (rx->valid()) {
        rx_sock_ = rx->socket();  // borrow before the move; used for dynamic listener joins
        reactor.add(std::move(rx));
    }

    // Inter-site UDPTUN (optional; any failure is non-fatal -- the entity runs its
    // local AVB streams normally without the tunnel). With a rendezvous server,
    // STUN traversal yields one shared socket for both directions; otherwise the
    // ingest/egress each open their own direct socket.
    if (!config_.udptun_rendezvous_server.empty()) {
        // Async punch-RETRY worker (not the one-shot blocking rendezvous): the two
        // sites' entities can't coordinate a single startup handshake, so retry
        // with a clock-derived rotating session id until data flows. See
        // start_udptun_punch_worker(). setup_udptun_rendezvous() remains for the
        // (unused) one-shot path / reference.
        (void)start_udptun_punch_worker();
    } else if (config_.udptun_enable && config_.udptun_egress && !config_.udptun_peer_host.empty()) {
        // Bidirectional direct peer: one shared socket so both ends transmitting
        // hole-punches both NAT pinholes without STUN.
        (void)setup_udptun_direct_shared();
    } else {
        (void)setup_udptun_ingest();
        (void)setup_udptun_egress();
    }

    running_ = true;
    return success();
}

auto AvbEntityAudioIO::setup_udptun_ingest() -> bool
{
    if (!config_.udptun_enable || config_.udptun_peer_host.empty()) {
        return false;
    }
    auto addr = net::SocketAddress::from_string(config_.udptun_peer_host, std::to_string(config_.udptun_peer_port));
    if (!addr) {
        std::print(stderr, "[udptun] cannot resolve peer {}:{}\n", config_.udptun_peer_host, config_.udptun_peer_port);
        return false;
    }
    udptun_peer_ = *addr;
    int const fd = ::socket(udptun_peer_.family(), SOCK_DGRAM, 0);
    if (fd < 0) {
        std::print(stderr, "[udptun] socket() failed\n");
        return false;
    }
    udptun_fd_ = net::FileDescriptor{fd};
    (void)net::set_nonblocking(fd);

    build_udptun_ingest_state();
    return true;
}

auto AvbEntityAudioIO::setup_udptun_direct_shared() -> bool
{
    // Direct-peer bidirectional: ONE socket bound to udptun_listen_port does both
    // ingest TX (sendto peer:peer_port) and egress RX. Both ends binding the same
    // port and both transmitting opens both NAT pinholes -- the same hole-punch
    // owlm uses in --time-source direct-peer mode -- so no STUN is needed. Used
    // when direct mode has BOTH ingest and egress enabled.
    auto addr = net::SocketAddress::from_string(config_.udptun_peer_host, std::to_string(config_.udptun_peer_port));
    if (!addr) {
        std::print(stderr, "[udptun] cannot resolve peer {}:{}\n", config_.udptun_peer_host, config_.udptun_peer_port);
        return false;
    }
    udptun_peer_ = *addr;
    int const fd = ::socket(udptun_peer_.family(), SOCK_DGRAM, 0);
    if (fd < 0) {
        std::print(stderr, "[udptun] shared socket() failed\n");
        return false;
    }
    udptun_fd_ = net::FileDescriptor{fd};
    (void)net::set_reuse_addr(fd);
    auto const bind_addr = (udptun_peer_.family() == AF_INET6) ? net::SocketAddress::ipv6_any(config_.udptun_listen_port)
                                                               : net::SocketAddress::ipv4_any(config_.udptun_listen_port);
    if (::bind(fd, bind_addr.sockaddr(), bind_addr.length()) != 0) {
        std::print(stderr, "[udptun] shared bind :{} failed\n", config_.udptun_listen_port);
        udptun_fd_ = net::FileDescriptor{};
        return false;
    }
    (void)net::set_nonblocking(fd);
    udptun_shared_socket_ = true;
    udptun_direct_shared_mode_ = true;
    // Arm udptun_punch_service()'s keepalive: a DIRECT-SHARED node that is
    // momentarily idle (no ingest audio) must still send the ~1 ms keepalive to
    // hold its NAT pinhole open, or the peer's packets are dropped and the tunnel
    // is one-directional until this node happens to transmit. The STUN punch only
    // armed this via the worker's socket hand-off, which never happens here -- so
    // set the install timestamp ourselves. (The STUN-only teardown/re-punch paths
    // in that service are gated off by udptun_direct_shared_mode_.)
    timespec ts{};
    int64_t const tai = (clock_gettime(CLOCK_REALTIME, &ts) == 0)
        ? ((static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL) + ts.tv_nsec + config_.udptun_tai_offset_ns)
        : 1;
    udptun_install_tai_ns_ = tai;
    udptun_last_rx_ns_ = tai;
    udptun_rx_baseline_ = udptun_any_rx_.load(std::memory_order_relaxed);
    udptun_saw_data_ = true;  // shared socket is "up" at bind; no STUN first-data grace
    std::print(
        "Inter-site UDPTUN: direct-peer shared socket :{} <-> {} (bidirectional hole-punch, no STUN)\n",
        config_.udptun_listen_port,
        udptun_peer_.to_string());
    build_udptun_ingest_state();
    build_udptun_egress_state();
    return true;
}

void AvbEntityAudioIO::udptun_ingest_silence(size_t const frames)
{
    // Feed `frames` of zero PCM to the ingest so the entity transmits silence as
    // if its listener source were sending zeros (or its packets were not
    // received). Reuses the normal ingest path (anchor + reframe + send).
    if (!udptun_enable_ || frames == 0) {
        return;
    }
    size_t const need = frames * static_cast<size_t>(channels_) * 4;
    if (udptun_silence_buf_.size() < need) {
        return;  // pre-sized in build_udptun_ingest_state; never grow on the hot path
    }
    udptun_ingest_audio(std::span<uint8_t const>{udptun_silence_buf_}.first(need), /*real_source=*/false);
}

void AvbEntityAudioIO::udptun_ingest_sweep(size_t const frames)
{
    // Generate `frames` of the logarithmic sweep on sweep_channel (silence on all
    // other channels), encode interleaved int32 BIG-ENDIAN (the tunnel codec's
    // network-order AAF int32 format -- same layout as udptun_ingest_am824_as_int32),
    // and feed it as the tunnel source. real_source=true so the keepalive/silence
    // paths stand down: the sweep IS the audio that opens/holds the NAT pinhole.
    if (!udptun_enable_ || frames == 0) {
        return;
    }
    size_t const need = frames * static_cast<size_t>(channels_) * 4;
    if (sweep_buf_.size() < need) {
        return;  // pre-sized in build_udptun_ingest_state; never grow on the hot path
    }
    size_t const sweep_ch = (config_.sweep_channel < channels_) ? config_.sweep_channel : 0;
    uint8_t* const out = sweep_buf_.data();
    for (size_t f = 0; f < frames; ++f) {
        float const sample_f = sweep_gen_.next();  // one sample period per frame
        float const clamped = (sample_f > 1.0F) ? 1.0F : ((sample_f < -1.0F) ? -1.0F : sample_f);
        auto const v = static_cast<int32_t>(clamped * 2147483647.0F);
        for (size_t ch = 0; ch < channels_; ++ch) {
            int32_t const s = (ch == sweep_ch) ? v : 0;
            size_t const off = ((f * static_cast<size_t>(channels_)) + ch) * 4;
            out[off + 0] = static_cast<uint8_t>((static_cast<uint32_t>(s) >> 24) & 0xFFU);  // big-endian (MSB first)
            out[off + 1] = static_cast<uint8_t>((static_cast<uint32_t>(s) >> 16) & 0xFFU);
            out[off + 2] = static_cast<uint8_t>((static_cast<uint32_t>(s) >> 8) & 0xFFU);
            out[off + 3] = static_cast<uint8_t>(static_cast<uint32_t>(s) & 0xFFU);
        }
    }
    udptun_ingest_audio(std::span<uint8_t const>{sweep_buf_}.first(need), /*real_source=*/true);
}

void AvbEntityAudioIO::build_udptun_ingest_state()
{
    // Our tunnel stream identity = the entity's EUI-64 with the redundancy flag bit
    // forced CLEAR, so the redundant copy (primary | UDPTUN_REDUN_BIT) is ALWAYS a
    // distinct stream_id even when the entity_id happens to have that bit set, and so
    // the egress can classify primary vs redundant. The bit lives in the EUI-64 b4
    // byte that owlm_analyze masks, so both copies still group as one logical sender.
    udptun_stream_id_.from_uint64(config_.entity_id.to_uint64() & ~UDPTUN_REDUN_BIT);
    udptun_redundant_id_.from_uint64(udptun_stream_id_.to_uint64() | UDPTUN_REDUN_BIT);

    uint16_t const frames = config_.udptun_frames_per_packet > 0 ? config_.udptun_frames_per_packet : 48;
    auto const interval_us = static_cast<uint32_t>((static_cast<uint64_t>(frames) * 1'000'000ULL) / SAMPLE_RATE);

    udptun::AafV1OverAnnexJCodec::Config cc{};
    cc.stream_id.from_uint64(udptun_stream_id_.to_uint64());
    // Tell the codec the real redundant id when redundancy is on (so its classify()/
    // sender_pair_id() distinguish the copies); else leave it equal to primary
    // (has_redundancy() == false -> legacy single-stream).
    cc.redundant_stream_id.from_uint64(config_.udptun_redundant ? udptun_redundant_id_.to_uint64() : udptun_stream_id_.to_uint64());
    cc.format = avtp::AafFormat::int_32bit;
    cc.sample_rate = avtp::AafSampleRate::rate_96_khz;
    cc.channels = static_cast<uint16_t>(channels_);
    cc.bit_depth = AAF_BIT_DEPTH;
    cc.samples_per_packet = frames;
    cc.interval_us = interval_us;
    udptun_codec_.emplace(cc);

    udptun::AudioIngest<>::Config ic{};
    ic.channels = static_cast<uint16_t>(channels_);
    ic.sample_rate_hz = SAMPLE_RATE;
    ic.bytes_per_sample = 4;
    ic.tunnel_frames_per_packet = frames;
    udptun_ingest_.emplace(ic);

    size_t const datagram = udptun::AafV1OverAnnexJCodec::header_size() + udptun_codec_->payload_bytes();
    udptun_txbuf_.assign(datagram, 0);
    // Pre-zeroed silence block: up to one media tick (SAMPLES_PER_PACKET+1 frames)
    // of interleaved int32, fed to the ingest by udptun_ingest_silence().
    udptun_silence_buf_.assign(static_cast<size_t>(SAMPLES_PER_PACKET + 1) * channels_ * 4, 0);
    // Scratch for transcoding an AM824 source's MBLA quadlets to int32 before
    // ingest (see udptun_ingest_am824_as_int32); generously sized, grows if a
    // packet ever exceeds it. Same byte count per sample (4), so size like silence.
    udptun_am824_transcode_buf_.assign(static_cast<size_t>(SAMPLES_PER_PACKET + 1) * channels_ * 4, 0);
    // Test-signal sweep: same per-tick sizing as silence; configure the generator
    // from the [sweep] config (logarithmic chirp on one channel).
    sweep_buf_.assign(static_cast<size_t>(SAMPLES_PER_PACKET + 1) * channels_ * 4, 0);
    sweep_gen_.configure(
        config_.sweep_f_start_hz,
        config_.sweep_f_end_hz,
        config_.sweep_duration_s,
        static_cast<double>(SAMPLE_RATE),
        config_.sweep_amplitude);
    udptun_seq_ = 0;
    udptun_anchored_ = false;
    sweep_tai_anchor_ns_ = 0;
    sweep_frames_emitted_ = 0;
    udptun_enable_ = true;

    // Redundancy: size the delay ring to ~temporal_shift worth of packets
    // (interval_us each), pre-sizing each slot's PCM buffer so the replay path never
    // allocates. udptun_redundant_id_ was derived above (distinct by UDPTUN_REDUN_BIT).
    if (config_.udptun_redundant) {
        int64_t const shift_us = config_.udptun_temporal_shift_ms * 1000;
        udptun_redun_depth_ = std::max<size_t>(1, static_cast<size_t>(shift_us / std::max<uint32_t>(1, interval_us)));
        size_t const ring_sz = udptun_redun_depth_ + 4;
        udptun_redun_ring_.assign(ring_sz, UdptunRedunSlot{});
        size_t const pcm_bytes = udptun_codec_->payload_bytes();
        for (auto& s : udptun_redun_ring_) {
            s.pcm.assign(pcm_bytes, 0);
        }
        udptun_redun_head_ = 0;
        std::print(
            "Inter-site UDPTUN redundancy: temporal_shift {} ms ({} packets), redundant stream_id 0x{:016x}\n",
            config_.udptun_temporal_shift_ms,
            udptun_redun_depth_,
            udptun_redundant_id_.to_uint64());
    }
    // 1472 = 1500 MTU - 20 (IPv4) - 8 (UDP). A datagram above this IP-fragments.
    char const* const frag = (datagram > 1472) ? "  *** > MTU: WILL IP-FRAGMENT ***" : "";
    std::print(
        "Inter-site UDPTUN ingest: AAF int32 ({} ch)  ({} frames / {} us, {}-byte datagram{})  TAI = realtime + {} ns\n",
        channels_,
        frames,
        interval_us,
        datagram,
        frag,
        config_.udptun_tai_offset_ns);
}

void AvbEntityAudioIO::udptun_send_encoded(
    ieee::Eui64 const& stream_id, uint32_t const sequence, int64_t const tai_ns, std::span<uint8_t const> const pcm)
{
    if (udptun_fd_.get() < 0 || !udptun_codec_) {
        return;
    }
    size_t const hdr = udptun_codec_->encode(udptun_txbuf_, stream_id, sequence, tai_ns, /*interval_us=*/1000);
    size_t const total = hdr + pcm.size();
    if (total > udptun_txbuf_.size()) {
        return;
    }
    std::memcpy(udptun_txbuf_.data() + hdr, pcm.data(), pcm.size());
    (void)::sendto(udptun_fd_.get(), udptun_txbuf_.data(), total, MSG_DONTWAIT, udptun_peer_.sockaddr(), udptun_peer_.length());
    udptun_tx_packets_.fetch_add(1, std::memory_order_relaxed);
}

void AvbEntityAudioIO::udptun_send(int64_t const tai_ns, std::span<uint8_t const> const pcm)
{
    uint32_t const seq = udptun_seq_++;
    udptun_send_encoded(udptun_stream_id_, seq, tai_ns, pcm);

    // Redundancy: store this primary in the delay ring and replay the one from
    // udptun_redun_depth_ sends ago as the redundant copy (same seq + TAI, the
    // distinct redundant stream_id, sent ~temporal_shift later).
    if (config_.udptun_redundant && !udptun_redun_ring_.empty()) {
        auto& cur = udptun_redun_ring_[udptun_redun_head_];
        cur.seq = seq;
        cur.tai = tai_ns;
        cur.valid = true;
        if (cur.pcm.size() >= pcm.size()) {
            std::memcpy(cur.pcm.data(), pcm.data(), pcm.size());
        }
        size_t const back = (udptun_redun_head_ + udptun_redun_ring_.size() - udptun_redun_depth_) % udptun_redun_ring_.size();
        auto const& rep = udptun_redun_ring_[back];
        if (rep.valid && rep.pcm.size() >= pcm.size()) {
            udptun_send_encoded(udptun_redundant_id_, rep.seq, rep.tai, std::span<uint8_t const>{rep.pcm}.first(pcm.size()));
        }
        udptun_redun_head_ = (udptun_redun_head_ + 1) % udptun_redun_ring_.size();
    }
}

auto AvbEntityAudioIO::setup_udptun_egress() -> bool
{
    if (!config_.udptun_egress) {
        return false;
    }
    int const fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::print(stderr, "[udptun] egress socket() failed\n");
        return false;
    }
    udptun_rx_fd_ = net::FileDescriptor{fd};
    (void)net::set_reuse_addr(fd);
    auto const bind_addr = net::SocketAddress::ipv4_any(config_.udptun_listen_port);
    if (::bind(fd, bind_addr.sockaddr(), bind_addr.length()) != 0) {
        std::print(stderr, "[udptun] egress bind :{} failed\n", config_.udptun_listen_port);
        udptun_rx_fd_ = net::FileDescriptor{};
        return false;
    }
    (void)net::set_nonblocking(fd);

    build_udptun_egress_state();
    return true;
}

void AvbEntityAudioIO::build_udptun_egress_state()
{
    uint16_t const frames = config_.udptun_frames_per_packet > 0 ? config_.udptun_frames_per_packet : 44;
    udptun::AudioEgress<>::Config ec{};
    ec.channels = static_cast<uint16_t>(channels_);
    ec.sample_rate_hz = SAMPLE_RATE;
    ec.bytes_per_sample = 4;
    ec.frames_per_packet = frames;
    ec.wcl_ns = config_.udptun_wcl_ns;
    udptun_egress_.emplace(ec);

    udptun::AafV1OverAnnexJCodec::Config dc{};
    dc.format = avtp::AafFormat::int_32bit;
    dc.sample_rate = avtp::AafSampleRate::rate_96_khz;
    dc.channels = static_cast<uint16_t>(channels_);
    dc.bit_depth = AAF_BIT_DEPTH;
    dc.samples_per_packet = frames;
    udptun_egress_codec_.emplace(dc);

    udptun_rxbuf_.assign(2048, 0);
    udptun_egress_pcm_.assign(static_cast<size_t>(SAMPLES_PER_PACKET + 1) * channels_ * 4, 0);
    udptun_egress_active_ = true;

    // Optional per-packet timing recorder (owlm UdpTunCsvRecord colbin schema).
    // Fully pre-allocated (never grows mid-run, so no mremap stalls the RT data
    // plane); sized by udptun.egress_colbin_max_mb (default 15 min @ 96 kHz).
    if (!config_.udptun_egress_colbin_path.empty()) {
        statusbar::colbin::WriterConfig const colbin_cfg{
            .max_capacity_bytes = config_.udptun_egress_colbin_max_bytes, .preallocate = true};
        auto w = statusbar::colbin::Writer::create(
            config_.udptun_egress_colbin_path, udptun::udptun_colbin_schema(), colbin_cfg);
        if (w) {
            udptun_egress_colbin_.emplace(std::move(*w));
        } else {
            std::print(
                stderr, "[udptun] egress colbin '{}' open failed: {}\n", config_.udptun_egress_colbin_path, w.error().message());
        }
    }
    std::print(
        "Inter-site UDPTUN egress: WCL {} ns  ({} frames/packet) -> local AVB talkers{}\n",
        config_.udptun_wcl_ns,
        frames,
        udptun_egress_colbin_ ? "  [+colbin timing]" : "");
}

auto AvbEntityAudioIO::setup_udptun_rendezvous() -> bool
{
    if (config_.udptun_rendezvous_server.empty()) {
        return false;
    }
    if (!config_.udptun_enable && !config_.udptun_egress) {
        std::print(stderr, "[udptun] rendezvous set but neither --udptun.enable nor --udptun.egress -- skipping\n");
        return false;
    }
    statusbar::crypto::Aes128SivKey key{};
    if (!net::parse_hex_into(config_.udptun_rendezvous_key, std::span<uint8_t>{key.data})) {
        std::print(stderr, "[udptun] rendezvous-key must be 64 hex chars\n");
        return false;
    }
    stun::SessionId session_id{};
    if (!net::parse_hex_into(config_.udptun_rendezvous_session_id, std::span<uint8_t>{session_id.bytes})) {
        std::print(stderr, "[udptun] rendezvous-session-id must be 32 hex chars\n");
        return false;
    }
    std::string host{};
    std::string port{};
    if (!net::split_host_port(config_.udptun_rendezvous_server, host, port)) {
        std::print(stderr, "[udptun] rendezvous-server must be HOST:PORT\n");
        return false;
    }
    auto server_addr = net::SocketAddress::from_string(host, port, net::SocketDatagram);
    if (!server_addr) {
        std::print(stderr, "[udptun] cannot resolve rendezvous server '{}:{}'\n", host, port);
        return false;
    }
    bool const responder = (config_.udptun_rendezvous_role == "responder");
    // Placeholder EUI-64 from session id + role byte; the server only needs the
    // two peers' EUI-64s to differ (mirrors owlm's perform_rendezvous_into).
    ieee::Eui64 client_eui{};
    auto eui_span = client_eui.span();
    for (size_t i = 0; i < eui_span.size(); ++i) {
        eui_span[i] = session_id.bytes[i];
    }
    eui_span[0] = responder ? 0x02U : 0x01U;

    stun::RendezvousConfig rcfg{
        .server_address = *server_addr,
        .session_id = session_id,
        .client_eui64 = client_eui,
        .role = responder ? stun::Role::Responder : stun::Role::Initiator,
        .shared_key = key,
        // Ephemeral local port (0), NOT udptun_listen_port. In rendezvous mode the
        // hole-punched socket is SHARED for TX+RX and the peer endpoint is learned
        // from the STUN XOR-MAPPED-ADDRESS, so the local port is immaterial to the
        // data plane. Pinning a fixed port (e.g. 17220 -- also used by the direct
        // and egress paths) invites an endpoint-dependent / reused NAT mapping whose
        // STUN-observed external port differs from the peer-facing pinhole, breaking
        // the punch. An ephemeral port gets a fresh mapping and reports the true
        // reflexive (matches the proven stun-client tool, which binds ephemeral).
        .local_port = 0,
        .local_interface = config_.interface_name,
        .timeout_ms = 60'000,
    };
    std::print(
        "Inter-site UDPTUN: STUN rendezvous against {} as {}...\n", server_addr->to_string(), config_.udptun_rendezvous_role);
    auto result = stun::perform_rendezvous(rcfg);
    if (!result) {
        std::print(stderr, "[udptun] rendezvous failed: {}\n", result.error().message());
        return false;
    }
    // Adopt the hole-punched socket as the SHARED TX+RX socket and the peer's
    // reflexive address as the data peer.
    udptun_fd_ = std::move(result->socket);
    (void)net::set_nonblocking(udptun_fd_.get());
    udptun_peer_ = result->peer_reflexive_address;
    udptun_rendezvous_active_ = true;
    udptun_shared_socket_ = true;
    std::print(
        "Inter-site UDPTUN: rendezvous done. local={} my reflexive={} peer={}\n",
        result->local_address.to_string(),
        result->my_reflexive_address.to_string(),
        udptun_peer_.to_string());

    if (config_.udptun_enable) {
        build_udptun_ingest_state();
    }
    if (config_.udptun_egress) {
        build_udptun_egress_state();
    }
    return true;
}

auto AvbEntityAudioIO::start_udptun_punch_worker() -> bool
{
    if (config_.udptun_rendezvous_server.empty()) {
        return false;
    }
    if (!config_.udptun_enable && !config_.udptun_egress) {
        std::print(stderr, "[udptun] punch: rendezvous set but neither enable nor egress -- skipping\n");
        return false;
    }
    // Build codec/buffer state up front (no socket) so the entity's local AVB runs
    // immediately; the media thread installs a hole-punched socket the moment the
    // worker stages one.
    if (config_.udptun_enable) {
        build_udptun_ingest_state();
    }
    if (config_.udptun_egress) {
        build_udptun_egress_state();
    }
    udptun_punch_run_.store(true);
    udptun_punch_thread_ = std::thread([this] { statusbar::run_guarded("udptun-punch", [this] { udptun_punch_loop(); }); });
    std::print(
        "Inter-site UDPTUN: STUN punch-retry worker started ({} as {}); local AVB runs now, tunnel comes up async\n",
        config_.udptun_rendezvous_server,
        config_.udptun_rendezvous_role);
    return true;
}

void AvbEntityAudioIO::stop_udptun_punch_worker()
{
    udptun_punch_run_.store(false);
    if (udptun_punch_thread_.joinable()) {
        udptun_punch_thread_.join();
    }
}

void AvbEntityAudioIO::udptun_punch_loop()
{
    using namespace std::chrono_literals;

    // Parse the static rendezvous inputs once.
    stun::SessionId base{};
    if (!net::parse_hex_into(config_.udptun_rendezvous_session_id, std::span<uint8_t>{base.bytes})) {
        std::print(stderr, "[udptun] punch: rendezvous-session-id must be 32 hex chars\n");
        return;
    }
    statusbar::crypto::Aes128SivKey key{};
    if (!net::parse_hex_into(config_.udptun_rendezvous_key, std::span<uint8_t>{key.data})) {
        std::print(stderr, "[udptun] punch: rendezvous-key must be 64 hex chars\n");
        return;
    }
    std::string host{};
    std::string port{};
    if (!net::split_host_port(config_.udptun_rendezvous_server, host, port)) {
        std::print(stderr, "[udptun] punch: rendezvous-server must be HOST:PORT\n");
        return;
    }
    auto server_addr = net::SocketAddress::from_string(host, port, net::SocketDatagram);
    if (!server_addr) {
        std::print(stderr, "[udptun] punch: cannot resolve rendezvous server '{}:{}'\n", host, port);
        return;
    }
    bool const responder = (config_.udptun_rendezvous_role == "responder");

    // Both peers attempt the SAME session id within each WINDOW, derived from the
    // shared GPS-NTP TAI clock (window counter overwrites the id's high 8 bytes).
    // Aligning attempts to window boundaries lands both nodes in the handshake
    // together with no external orchestration, and rotating the id every window
    // avoids stale STUN-server state -- exactly what owlm's harness achieves with a
    // fresh id per retry. The tunnel, once up, persists; the worker only re-punches
    // when the media-thread watchdog (udptun_punch_service) reports lost data.
    constexpr int64_t WINDOW_NS = 15'000'000'000LL;  // 15 s rendezvous window

    auto tai_now = [this]() -> int64_t {
        timespec ts{};
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
            return 0;
        }
        return (static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL) + ts.tv_nsec + config_.udptun_tai_offset_ns;
    };

    while (udptun_punch_run_.load(std::memory_order_relaxed)) {
        int64_t const now = tai_now();
        int64_t const window = (now > 0) ? now / WINDOW_NS : 0;
        int64_t const into_window = now - (window * WINDOW_NS);
        int64_t const remain_ms = (WINDOW_NS - into_window) / 1'000'000LL;

        stun::SessionId sid = base;
        for (int i = 0; i < 8; ++i) {
            sid.bytes[i] = static_cast<uint8_t>((window >> (8 * (7 - i))) & 0xFF);
        }
        ieee::Eui64 client_eui{};
        auto eui_span = client_eui.span();
        for (size_t i = 0; i < eui_span.size(); ++i) {
            eui_span[i] = sid.bytes[i];
        }
        eui_span[0] = responder ? 0x02U : 0x01U;

        stun::RendezvousConfig rcfg{
            .server_address = *server_addr,
            .session_id = sid,
            .client_eui64 = client_eui,
            .role = responder ? stun::Role::Responder : stun::Role::Initiator,
            .shared_key = key,
            .local_port = 0,
            .local_interface = config_.interface_name,
            .timeout_ms = static_cast<uint32_t>(std::clamp<int64_t>(remain_ms - 1000, 3000, 12000)),
        };

        auto result = stun::perform_rendezvous(rcfg);
        if (!udptun_punch_run_.load(std::memory_order_relaxed)) {
            break;
        }
        if (result) {
            {
                std::lock_guard<std::mutex> lk(udptun_stage_mutex_);
                udptun_staged_fd_ = std::move(result->socket);
                udptun_staged_peer_ = result->peer_reflexive_address;
                udptun_staged_ready_ = true;
            }
            udptun_punch_retry_.store(false);
            std::print(
                "[udptun] punch: paired (window {}) peer={} -- handing socket to media thread\n",
                window,
                result->peer_reflexive_address.to_string());
            // Hold while the tunnel is up; the media thread sets udptun_punch_retry_
            // if RX never starts or a live stream stalls, prompting a fresh punch.
            while (udptun_punch_run_.load(std::memory_order_relaxed) && !udptun_punch_retry_.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(250ms);
            }
            udptun_punch_retry_.store(false);
        } else {
            std::print(stderr, "[udptun] punch: window {} no pair ({}) -- retry next window\n", window, result.error().message());
        }

        // Align the next attempt to the next window boundary so both peers fire
        // together; sleep in small chunks for responsive shutdown.
        int64_t const t2 = tai_now();
        int64_t const next_boundary = (((t2 > 0 ? t2 : 0) / WINDOW_NS) + 1) * WINDOW_NS;
        int64_t sleep_ns = next_boundary - t2;
        while (udptun_punch_run_.load(std::memory_order_relaxed) && sleep_ns > 0) {
            int64_t const chunk = std::min<int64_t>(sleep_ns, 200'000'000LL);
            std::this_thread::sleep_for(std::chrono::nanoseconds(chunk));
            sleep_ns -= chunk;
        }
    }
}

void AvbEntityAudioIO::udptun_punch_service(int64_t now_tai_ns)
{
    // 1) Install a staged hole-punched socket (worker -> media handoff). Only this
    // (media) thread ever assigns udptun_fd_ during operation.
    {
        std::lock_guard<std::mutex> lk(udptun_stage_mutex_);
        if (udptun_staged_ready_) {
            udptun_fd_ = std::move(udptun_staged_fd_);
            udptun_peer_ = udptun_staged_peer_;
            udptun_staged_ready_ = false;
            (void)net::set_nonblocking(udptun_fd_.get());
            udptun_shared_socket_ = true;
            udptun_rendezvous_active_ = true;
            udptun_anchored_ = false;
            sweep_tai_anchor_ns_ = 0;
            sweep_frames_emitted_ = 0;  // re-anchor ingest TAI on the fresh socket
            udptun_install_tai_ns_ = now_tai_ns;
            udptun_rx_baseline_ = udptun_any_rx_.load(std::memory_order_relaxed);
            udptun_last_rx_ns_ = now_tai_ns;
            udptun_saw_data_ = false;
            // Re-anchor the egress on the fresh tunnel: drop any stale timeline so a
            // re-punch never leaves it stuck emitting silence (it re-anchors on the
            // first packet of the new stream). Reset the egress-silence watchdog too.
            if (udptun_egress_) {
                udptun_egress_->reset();
            }
            udptun_egress_play_baseline_ = udptun_egress_real_frames_;
            udptun_egress_last_play_ns_ = now_tai_ns;
            // No std::print on the RT media thread: the non-RT punch worker already
            // logs "paired ... peer=" just before handing the socket over.
        }
    }

    if (udptun_install_tai_ns_ == 0 || udptun_fd_.get() < 0) {
        return;  // no active tunnel socket
    }

    // 1b) Keepalive: a silence_source node already streams continuously, but a node
    // whose tunnel source is a (possibly-disconnected) AVB listener sends nothing
    // when idle -- so its NAT pinhole never opens and the punch can't complete until
    // audio happens to start. Emit a tiny raw datagram (NOT through the codec, so no
    // cross-thread ingest race) straight to the peer while idle, so both pinholes
    // open and stay warm; real audio takes over seamlessly when it arrives. The far
    // end drops the bad decode but still counts it as tunnel liveness (any_rx).
    if (!config_.udptun_silence_source) {
        int64_t const last_audio = udptun_last_real_ingest_tai_.load(std::memory_order_relaxed);
        bool const streaming = (last_audio != 0) && (now_tai_ns - last_audio < 100'000'000LL);
        // ~1 ms cadence (matches owlm's proven punch rate + the ~2000 pkt/s real
        // audio that opened the pinhole on hardware). A slow keepalive (e.g. 50 ms)
        // refreshes a mapping but does NOT reliably open one on these home NATs.
        if (!streaming && (now_tai_ns - udptun_last_keepalive_ns_ > 1'000'000LL)) {
            udptun_last_keepalive_ns_ = now_tai_ns;
            std::array<uint8_t, 16> ka{};  // bare keepalive; opens/holds the NAT pinhole
            (void)::sendto(udptun_fd_.get(), ka.data(), ka.size(), MSG_DONTWAIT, udptun_peer_.sockaddr(), udptun_peer_.length());
        }
    }

    // 2) Watchdog liveness via ANY received datagram (audio OR keepalive) -- the far
    // end's packets prove the punch actually opened (STUN pairing alone does not).
    // Counting keepalives keeps an idle-but-open pinhole warm, so when real audio
    // starts it flows immediately instead of waiting on a fresh punch. No first
    // packet within the grace window, or a live tunnel going silent, tears the
    // socket down and asks the worker to re-punch.
    uint64_t const rx = udptun_any_rx_.load(std::memory_order_relaxed);
    if (rx != udptun_rx_baseline_) {
        udptun_rx_baseline_ = rx;
        udptun_last_rx_ns_ = now_tai_ns;
        udptun_saw_data_ = true;
    }

    // 2b) Egress-anchor self-heal: distinct from the socket teardown below (which
    // fires only when data STOPS). This catches DECODED tunnel AUDIO arriving while
    // the egress emits no real audio for a sustained window -- i.e. the playout
    // timeline is stale (the punch->egress startup race that left jdk01a silent until
    // a 2nd restart). Reset() it so it re-anchors on the next packet. Gated on real
    // audio (udptun_rx_packets_, decoded) not keepalives, so an idle tunnel doesn't
    // trip it; the ~22 ms WCL startup gap is far shorter than the 2 s window.
    if (udptun_egress_) {
        uint64_t const audio_rx = udptun_rx_packets_.load(std::memory_order_relaxed);
        if (audio_rx != udptun_egress_audio_rx_baseline_) {
            udptun_egress_audio_rx_baseline_ = audio_rx;
            udptun_egress_last_audio_ns_ = now_tai_ns;
        }
        if (udptun_egress_real_frames_ != udptun_egress_play_baseline_) {
            udptun_egress_play_baseline_ = udptun_egress_real_frames_;  // egress producing audio -> healthy
            udptun_egress_last_play_ns_ = now_tai_ns;
            udptun_egress_reset_streak_ = 0;  // recovered -> clear the escalation streak
        } else if (
            (now_tai_ns - udptun_egress_last_audio_ns_ < 1'000'000'000LL) &&
            (now_tai_ns - udptun_egress_last_play_ns_ > 2'000'000'000LL)) {
            udptun_egress_last_play_ns_ = now_tai_ns;  // grace before re-checking
            // A bare anchor reset recovers the common punch->egress startup race in
            // one shot. But if decoded audio keeps arriving and the egress STILL
            // plays nothing after several resets, the timeline itself is unworkable
            // -- e.g. the far end restarted onto a higher-latency re-punched path, so
            // transit now exceeds WCL and the read position permanently sits ahead of
            // the newest arrived packet. No anchor reset can fix that; only a fresh
            // tunnel can (a lower-latency re-punch, or the far end settling). The
            // plain stall teardown below never fires here because data is still
            // flowing -- so escalate to a re-punch ourselves, which is what a manual
            // restart did by hand (observed: 1090 fruitless resets, then a restart
            // fixed it). constexpr threshold ~= reset cadence (2 s) * count.
            // RT path: never std::print here (stderr I/O can block/alloc on the media
            // thread). Bump an atomic counter; print_state() surfaces it off-thread.
            // Escalate to a re-punch only on the STUN tunnel -- a DIRECT-SHARED
            // socket has no punch worker to re-create it, so tearing it down would
            // strand the tunnel. There the continuous keepalive + a fresh anchor
            // reset are the recovery (when the peer resumes, both pinholes re-open).
            constexpr int kEgressResetEscalate = 3;
            if (!udptun_direct_shared_mode_ && ++udptun_egress_reset_streak_ >= kEgressResetEscalate) {
                udptun_egress_reset_streak_ = 0;
                udptun_egress_repunch_count_.fetch_add(1, std::memory_order_relaxed);
                udptun_fd_ = net::FileDescriptor{};  // close -> forces a fresh punch
                udptun_shared_socket_ = false;
                udptun_rendezvous_active_ = false;
                udptun_install_tai_ns_ = 0;
                udptun_punch_retry_.store(true);  // wake the worker for a fresh window punch
                return;                           // socket gone; nothing else to service this tick
            }
            udptun_egress_reset_count_.fetch_add(1, std::memory_order_relaxed);
            udptun_egress_->reset();
        }
    }

    // A residential-NAT hole-punch can take tens of seconds to actually open even
    // when it will succeed, so give first data a generous window before giving up
    // (tearing down too early kills a punch that was about to come through). Once a
    // stream is live, a much shorter stall triggers a re-punch.
    constexpr int64_t GRACE_NS = 45'000'000'000LL;  // first data must arrive within 45 s of install
    constexpr int64_t STALL_NS = 8'000'000'000LL;   // a live stream silent this long -> re-punch
    int64_t const since_install = now_tai_ns - udptun_install_tai_ns_;
    int64_t const since_rx = now_tai_ns - udptun_last_rx_ns_;
    bool const dead = (!udptun_saw_data_ && since_install > GRACE_NS) || (udptun_saw_data_ && since_rx > STALL_NS);
    // Only the STUN tunnel tears down + re-punches on a stall; DIRECT-SHARED has no
    // worker to re-punch, and its keepalive keeps the pinhole warm so it re-converges
    // on its own when the peer comes back.
    if (dead && !udptun_direct_shared_mode_) {
        // RT path: count, don't print (the worker logs the ensuing re-punch).
        udptun_egress_repunch_count_.fetch_add(1, std::memory_order_relaxed);
        udptun_fd_ = net::FileDescriptor{};  // close
        udptun_shared_socket_ = false;
        udptun_rendezvous_active_ = false;
        udptun_install_tai_ns_ = 0;
        udptun_punch_retry_.store(true);  // wake the worker for a fresh window punch
    }
}

void AvbEntityAudioIO::udptun_egress_drain_rx()
{
    int const rx_fd = udptun_rx_fd();
    if (rx_fd < 0 || !udptun_egress_ || !udptun_egress_codec_) {
        return;
    }
    size_t const frame_bytes = static_cast<size_t>(channels_) * 4;
    if (frame_bytes == 0) {
        return;
    }
    for (int guard = 0; guard < 512; ++guard) {  // bound the drain per tick
        ssize_t const n = ::recv(rx_fd, udptun_rxbuf_.data(), udptun_rxbuf_.size(), MSG_DONTWAIT);
        if (n <= 0) {
            break;
        }
        // Count EVERY datagram (decodable audio or a bare keepalive) for the punch
        // watchdog's tunnel-liveness check -- a keepalive proves the pinhole is open.
        udptun_any_rx_.fetch_add(1, std::memory_order_relaxed);
        auto const dec = udptun_egress_codec_->decode(std::span<uint8_t const>{udptun_rxbuf_.data(), static_cast<size_t>(n)});
        if (!dec) {
            continue;
        }
        size_t const pcm_bytes = dec->audio.size();
        if (pcm_bytes < frame_bytes) {
            continue;
        }
        auto const nf = static_cast<uint16_t>(pcm_bytes / frame_bytes);
        int64_t const pt_ns = udptun_egress_codec_->tx_gptp_ns(*dec);
        (void)udptun_egress_->submit(pt_ns, dec->audio.first(static_cast<size_t>(nf) * frame_bytes), nf);
        udptun_rx_packets_.fetch_add(1, std::memory_order_relaxed);

        // Per-packet timing: latency = local rx TAI - the packet's TAI
        // presentation time. Same TAI basis (CLOCK_REALTIME + tai_offset) the
        // ingest stamps with, so both ends share one absolute timeline.
        if (udptun_egress_colbin_) {
            timespec ts{};
            if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
                int64_t const rx_tai =
                    (static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL) + ts.tv_nsec + config_.udptun_tai_offset_ns;
                // Classify primary vs redundant by the REDUN_BIT; record the
                // PRIMARY-form stream_id for both so owlm_analyze groups them as
                // one logical stream (role distinguishes them for recovery accounting).
                uint64_t const sid_u64 = dec->pdu.stream_id().to_uint64();
                bool const is_redun = config_.udptun_redundant && ((sid_u64 & UDPTUN_REDUN_BIT) != 0);
                ieee::Eui64 sender{};
                sender.from_uint64(sid_u64 & ~UDPTUN_REDUN_BIT);
                auto const role = !config_.udptun_redundant
                    ? udptun::PacketRole::RemoteLegacy
                    : (is_redun ? udptun::PacketRole::RemoteRedundant : udptun::PacketRole::RemotePrimary);
                udptun::UdpTunCsvRecord rec{
                    .rx_gptp_ns = rx_tai,
                    .presentation_time_ns = pt_ns,
                    .latency_ns = rx_tai - pt_ns,
                    .sender_id = sender,
                    .sequence = dec->pdu.get_sequence_num(),
                    .interval_us = static_cast<uint32_t>((static_cast<uint64_t>(nf) * 1'000'000ULL) / SAMPLE_RATE),
                    .role = static_cast<uint8_t>(role),
                    ._pad = {},
                };
                std::array<uint8_t, sizeof(udptun::UdpTunCsvRecord)> row{};
                std::memcpy(row.data(), &rec, sizeof(rec));
                (void)udptun_egress_colbin_->write_row(row);
            }
        }
    }
}

void AvbEntityAudioIO::udptun_egress_fill(int64_t const now_tai_ns, size_t const samples)
{
    if (!udptun_egress_) {
        return;
    }
    size_t const frame_bytes = static_cast<size_t>(channels_) * 4;
    size_t const need = samples * frame_bytes;
    if (udptun_egress_pcm_.size() < need) {
        return;
    }
    // Count frames the egress filled with REAL audio (vs zero-fill concealment) so
    // the punch-service watchdog can tell "tunnel delivering but egress silent"
    // (stale anchor) from "tunnel genuinely idle". Media-thread only -> plain add.
    udptun_egress_real_frames_ +=
        udptun_egress_->playout(now_tai_ns, std::span<uint8_t>{udptun_egress_pcm_}.first(need), static_cast<uint16_t>(samples));
    // Interleaved int32 (network byte order) -> float audio_buffer_. Missing
    // frames were zero-filled by playout(), so underrun becomes silence.
    for (size_t i = 0; i < samples; ++i) {
        for (size_t ch = 0; ch < channels_; ++ch) {
            size_t const off = ((i * channels_) + ch) * 4;
            auto const v = static_cast<int32_t>(
                (static_cast<uint32_t>(udptun_egress_pcm_[off]) << 24) |
                (static_cast<uint32_t>(udptun_egress_pcm_[off + 1]) << 16) |
                (static_cast<uint32_t>(udptun_egress_pcm_[off + 2]) << 8) | static_cast<uint32_t>(udptun_egress_pcm_[off + 3]));
            audio_buffer_[(i * channels_) + ch] = static_cast<float>(v) / 2147483648.0F;
        }
    }
}

auto AvbEntityAudioIO::stop() -> Status
{
    if (!running_) {
        return failure(std::make_error_code(std::errc::not_connected));
    }

    stop_udptun_punch_worker();
    auto const now = TimePoint{std::chrono::steady_clock::now().time_since_epoch()};
    (void)components_.adp_advertiser.stop(now);
    net_handlers_.reset();
    if (udptun_egress_colbin_) {
        (void)udptun_egress_colbin_->commit();
        udptun_egress_colbin_.reset();
    }
    running_ = false;
    return success();
}

//
// State queries
//

auto AvbEntityAudioIO::is_ready() const noexcept -> bool
{
    return supervisor_.current_state() == nanoavb::supervisor_sm::Def::State::Ready;
}

auto AvbEntityAudioIO::state_string() const -> std::string_view
{
    using nanoavb::supervisor_sm::Def;
    switch (supervisor_.current_state()) {
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

void AvbEntityAudioIO::print_state() const
{
    std::print(
        "State: supervisor={} gptp={} mvrp={} acmp[am824={} aaf={}] channels={} | "
        "AM824 tx={} rx={} rx_samples={} rx_bad={} | AAF tx={} rx={} rx_samples={} rx_bad={} | "
        "egress[resets={} repunch={}]\n",
        state_string(),
        gptp_ctx_.time_locked ? "Locked" : "Unlocked",
        mvrp_ctx_.joined ? "Joined" : "NotJoined",
        components_.acmp_talker.connection_count(AM824_STREAM_INDEX),
        components_.acmp_talker.connection_count(AAF_STREAM_INDEX),
        channels_,
        am824_tx_packets_,
        am824_rx_packets_.load(std::memory_order_relaxed),
        am824_rx_samples_.load(std::memory_order_relaxed),
        am824_rx_bad_.load(std::memory_order_relaxed),
        aaf_tx_packets_,
        aaf_rx_packets_.load(std::memory_order_relaxed),
        aaf_rx_samples_.load(std::memory_order_relaxed),
        aaf_rx_bad_.load(std::memory_order_relaxed),
        udptun_egress_reset_count_.load(std::memory_order_relaxed),
        udptun_egress_repunch_count_.load(std::memory_order_relaxed));
}

//
// MSRP talker reservation (advertise the AM824 stream)
//

auto AvbEntityAudioIO::make_talker_srp_info(uint16_t stream_index) const -> nanoavb::TalkerStreamSrpInfo
{
    nanoavb::TalkerStreamSrpInfo info{};
    if (auto const* stream = components_.acmp_talker.get_stream(stream_index); stream != nullptr) {
        (void)statusbar::tsn::load_unchecked(stream->stream_id.span(), &info.stream_id);
        info.dest_address = stream->stream_dest_mac;
        info.vlan_id = stream->stream_vlan_id;
    } else {
        info.vlan_id = config_.vlan_id;
    }
    info.max_interval_frames = 1;
    constexpr uint16_t QUADLET_BYTES = 4;
    if (stream_index == CRF_STREAM_INDEX) {
        // CRF: 20-byte header + timestamps_per_packet * 8-byte timestamps (no audio).
        info.max_frame_size =
            static_cast<uint16_t>(avtp::CrfPdu::HEADER_LENGTH + (config_.crf_timestamps_per_packet * avtp::CrfPdu::TIMESTAMP_SIZE));
    } else {
        // L2 payload size per packet: AVTP header (32 for AM824/61883, 24 for AAF)
        // plus one packet of audio quadlets (channels * samples * 4 bytes). +1
        // sample of headroom: GPS-rate pacing emits nominal or nominal+/-1.
        uint16_t const header_bytes = (stream_index == AAF_STREAM_INDEX) ? avtp::AafPdu::HEADER_LENGTH : uint16_t{32};
        info.max_frame_size = static_cast<uint16_t>(header_bytes + ((SAMPLES_PER_PACKET + 1) * channels_ * QUADLET_BYTES));
    }
    info.accumulated_latency = 0;
    return info;
}

//
// Event handlers
//

void AvbEntityAudioIO::on_link_up(TimePoint time)
{
    supervisor_ctx_.link_up = true;
    supervisor_.handle_event(supervisor_ctx_, nanoavb::supervisor_sm::Def::Event::LinkUp, time);
}

void AvbEntityAudioIO::on_link_down(TimePoint time)
{
    supervisor_ctx_.link_up = false;
    supervisor_.handle_event(supervisor_ctx_, nanoavb::supervisor_sm::Def::Event::LinkDown, time);
}

void AvbEntityAudioIO::on_gptp_announce(TimePoint time, bool has_grandmaster)
{
    if (has_grandmaster && !gptp_ctx_.time_locked) {
        gptp_.handle_event(gptp_ctx_, nanoavb::gptp_sm::Def::Event::LockedStable, time);
    }
}

void AvbEntityAudioIO::on_timeout(TimePoint time)
{
    using nanoavb::supervisor_sm::Def;
    if (supervisor_.current_state() == Def::State::Init) {
        supervisor_.handle_event(supervisor_ctx_, Def::Event::Timeout, time);
    }
}

//
// Audio processing
//

void AvbEntityAudioIO::process_audio(TimePoint time)
{
    auto const base_now_ns = static_cast<uint64_t>(time.time_since_epoch().count());
    // One packet's worth of gPTP time: 12 samples @ 96 kHz = 125 us.
    constexpr uint64_t PACKET_INTERVAL_NS = (static_cast<uint64_t>(SAMPLES_PER_PACKET) * 1'000'000'000ULL) / SAMPLE_RATE;

    // Emit config_.packets_per_wake packets per stream this wake. Each packet
    // carries a fresh SAMPLES_PER_PACKET block (oscillators advance) and an
    // avtp_timestamp one packet interval later than the previous, so the on-wire
    // cadence (8000 x 12-sample Class A frames/s) and the listener's expected
    // 125 us timestamp step are UNCHANGED -- only the media-timer WAKE rate drops
    // by packets_per_wake, giving that many fewer deadlines to miss. The
    // burst-of-N egress is not strict Class A shaping; presentation_offset_ns
    // (>> N*125 us) absorbs it at the listener. See packets_per_wake doc.
    // Re-estimate the GPS frequency ratio that pins the media-clock rate. When
    // media_lock_to_gptp is set, r_ stays 1.0 (media clock == gPTP) and we skip the
    // CLOCK_REALTIME sampling entirely -- see Config::media_lock_to_gptp.
    if (!config_.media_lock_to_gptp) {
        update_gps_ratio(base_now_ns);
    }

    // Inter-site rendezvous punch-retry (media-thread half): install a freshly
    // hole-punched socket staged by the worker and watchdog the RX. Runs before
    // the drain so a just-installed socket is drained this same wake.
    if (udptun_punch_run_.load(std::memory_order_relaxed)) {
        timespec pts{};
        int64_t punch_tai = 0;
        if (clock_gettime(CLOCK_REALTIME, &pts) == 0) {
            punch_tai = (static_cast<int64_t>(pts.tv_sec) * 1'000'000'000LL) + pts.tv_nsec + config_.udptun_tai_offset_ns;
        }
        udptun_punch_service(punch_tai);
    }

    // Inter-site egress: drain the UDP socket and snapshot the TAI playout clock
    // once per wake (CLOCK_REALTIME + offset). Done on this (media-timer) thread so
    // AudioEgress stays single-threaded.
    int64_t udptun_now_tai_ns = 0;
    if (udptun_egress_active_) {
        udptun_egress_drain_rx();
        // Tunnel playout clock = GPS-TAI derived from the gPTP master via the TAI
        // translator (NOT raw CLOCK_REALTIME): the gPTP PHC gives a smooth,
        // jitter-free rate and the Kalman offset pins it to absolute GPS-TAI, so
        // this matches the rate the far ingest stamps with -> zero buffer drift.
        // Falls back to raw CLOCK_REALTIME+offset until the translator has a sample.
        if (tai_translator_.has_sample()) {
            udptun_now_tai_ns = tai_translator_.tai_ns(static_cast<int64_t>(base_now_ns));
        } else {
            timespec ts{};
            if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
                udptun_now_tai_ns = (static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL) + ts.tv_nsec + config_.udptun_tai_offset_ns;
            }
        }
    }

    // Inter-site ingest silence-source gate (decided once per wake). The silence
    // source keeps the tunnel TX (and its NAT pinhole) alive ONLY while no real
    // AVTP audio is arriving. The instant the listener source delivers packets,
    // the reactor thread feeds those frames straight into the ingest
    // (on_stream_rx_frame -> udptun_ingest_audio); this media-timer thread MUST
    // stand down, or the two threads would both submit to the same reframer --
    // double-feeding it (2x frame rate, TAI running ahead) and racing its
    // non-thread-safe state. Mirrors the keepalive `streaming` predicate so real
    // audio always wins and is forwarded cleanly to the peer.
    bool udptun_emit_silence = false;
    if (udptun_enable_ && config_.udptun_silence_source) {
        int64_t now_tai = udptun_now_tai_ns;
        if (now_tai == 0) {
            timespec ts{};
            if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
                now_tai = (static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL) + ts.tv_nsec + config_.udptun_tai_offset_ns;
            }
        }
        int64_t const last_audio = udptun_last_real_ingest_tai_.load(std::memory_order_relaxed);
        bool const streaming = (last_audio != 0) && (now_tai != 0) && (now_tai - last_audio < 100'000'000LL);
        udptun_emit_silence = !streaming;
    }

    for (size_t p = 0; p < config_.packets_per_wake; ++p) {
        uint64_t const wake_ns = base_now_ns + (static_cast<uint64_t>(p) * PACKET_INTERVAL_NS);
        // Publish the gPTP media time so the RX thread can judge LATE/EARLY_TIMESTAMP
        // against gPTP (the reactor's own clock is monotonic, not gPTP).
        last_gptp_ns_.store(wake_ns, std::memory_order_relaxed);

        // Deterministic, GPS-rate-pinned media clock: how many samples to emit
        // this tick (nominal +/- 1, paced to GPS) and the jitter-free presentation
        // timestamp of the packet's first sample. The wake time only paces the
        // count; the timestamp does NOT carry its jitter.
        auto const tick = media_clock_.advance(wake_ns, r_, static_cast<uint32_t>(SAMPLES_PER_PACKET));
        auto const samples = static_cast<size_t>(tick.samples);
        if (samples == 0) {
            continue;
        }

        // Source: generate the per-channel sine into the interleaved buffer.
        for (size_t i = 0; i < samples; ++i) {
            for (size_t ch = 0; ch < channels_; ++ch) {
                audio_buffer_[(i * channels_) + ch] = oscillators_[ch](0.0F);
            }
        }
        // Per-channel biquad filters (interleaved layout).
        for (size_t i = 0; i < samples; ++i) {
            for (size_t ch = 0; ch < channels_; ++ch) {
                audio_buffer_[(i * channels_) + ch] = biquads_[ch](audio_buffer_[(i * channels_) + ch]);
            }
        }

        // Inter-site egress: if active, replace the oscillator content with the
        // de-tunneled audio for this packet's presentation time (now + p*125us).
        // The talkers below then emit the received stream instead of the test tone.
        if (udptun_egress_active_ && udptun_now_tai_ns != 0) {
            int64_t const pkt_tai = udptun_now_tai_ns + (static_cast<int64_t>(p) * static_cast<int64_t>(PACKET_INTERVAL_NS));
            udptun_egress_fill(pkt_tai, samples);
        }

        // Inter-site ingest silence source: emit zero PCM at the media cadence so
        // the entity transmits silence as if its listener source were sending
        // zeros (no real talker). Keeps the reverse tunnel + its NAT pinhole warm.
        // Gated (udptun_emit_silence, decided once per wake) to stand down the
        // instant real AVTP audio arrives -- otherwise the silence would
        // double-feed and race the reactor thread's real-audio ingest.
        if (udptun_enable_ && config_.sweep_enable && udptun_now_tai_ns != 0) {
            // Test-signal mode: the logarithmic sweep IS the tunnel source. Pace it
            // by the GPS-TAI tunnel clock (udptun_now_tai_ns = tai_translator_ TAI,
            // gPTP-rate / GPS-epoch) -- exactly SAMPLE_RATE frames per second of TAI.
            // Emit the cumulative frame count implied by elapsed TAI, so the ingest
            // avtp_timestamp stays locked to TAI with zero drift and the far egress
            // (playing on the same GPS-TAI clock) never under/over-runs.
            int64_t const pkt_tai = udptun_now_tai_ns + (static_cast<int64_t>(p) * static_cast<int64_t>(PACKET_INTERVAL_NS));
            if (sweep_tai_anchor_ns_ == 0) {
                sweep_tai_anchor_ns_ = pkt_tai;
            }
            int64_t const elapsed = pkt_tai - sweep_tai_anchor_ns_;
            auto const target = (elapsed > 0)
                ? static_cast<uint64_t>((elapsed * static_cast<int64_t>(SAMPLE_RATE)) / 1'000'000'000LL)
                : uint64_t{0};
            if (target > sweep_frames_emitted_) {
                size_t n = static_cast<size_t>(target - sweep_frames_emitted_);
                size_t const cap = static_cast<size_t>(SAMPLES_PER_PACKET) * 4;  // bound catch-up bursts
                if (n > cap) {
                    n = cap;
                }
                udptun_ingest_sweep(n);
                sweep_frames_emitted_ += n;
            }
        } else if (udptun_emit_silence) {
            udptun_ingest_silence(samples);
        }

        if (audio_callback_) {
            audio_callback_(std::span{audio_buffer_}.first(samples * channels_), samples);
        }

        // Transmit both formats from the same audio with the deterministic
        // presentation timestamp of this packet's first sample. We do NOT gate on
        // the announce-based supervisor lock; process_audio() only runs while the
        // PTP bridge is healthy (the media timer fires), so we have a usable clock.
        uint64_t const pts_base = media_clock_.timestamp_for(tick.first_index);
        // Gate each talker stream on a ready/connected listener (SRP): don't put
        // a stream on the wire when nobody is listening. The CRF media clock is
        // emitted whenever any audio talker is transmitting (its listeners lock
        // to it for the audio streams).
        // Steady-clock now for the MSRP-ready grace window (same clock the
        // on_talker_listener callback stamps msrp_ready_ns_ with).
        int64_t const now_steady_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        bool const tx_am824 = talker_should_transmit(AM824_STREAM_INDEX, now_steady_ns);
        bool const tx_aaf = talker_should_transmit(AAF_STREAM_INDEX, now_steady_ns);
        if (tx_am824) {
            // AM824 tolerates the GPS-paced variable block count directly.
            transmit_am824(pts_base, tick.samples);
        }
        if (tx_aaf) {
            // AAF must be constant-size: buffer this wake's variable samples and
            // emit only whole SAMPLES_PER_PACKET blocks (0, 1, or 2+ this wake);
            // the <block remainder carries to the next wake. Each block's
            // avtp_timestamp is the jitter-free media-clock time of its first
            // sample, so the on-wire cadence stays a clean 12-sample step.
            aaf_reframer_.push(
                std::span<float const>{audio_buffer_}.first(samples * channels_), static_cast<uint16_t>(samples), tick.first_index);
            aaf_reframer_.drain([this](uint64_t first_index, std::span<float const> block) {
                transmit_aaf(media_clock_.timestamp_for(first_index), static_cast<uint16_t>(SAMPLES_PER_PACKET), block);
            });
        } else {
            // Gate closed: drop any partial block so a later reconnect starts
            // clean (no stale samples / stale timestamps).
            aaf_reframer_.clear();
        }
        // CRF media-clock PDU, decimated to its declared rate. One PDU carries
        // crf_timestamps_per_packet timestamps, each spaced sample_stride =
        // interval * SAMPLE_RATE / CRF_BASE_FREQUENCY of our 96 kHz samples, so a PDU
        // spans (ts_per_pkt * sample_stride) samples = pkts_per_crf audio packets. At
        // the Milan 48 kHz/interval-96/1-ts format that is 192 samples = every 16
        // audio packets -> 500 PDU/s, matching what Milan CRF inputs expect.
        if (tx_am824 || tx_aaf || talker_should_transmit(CRF_STREAM_INDEX, now_steady_ns)) {
            uint32_t const sample_stride = static_cast<uint32_t>(config_.crf_timestamp_interval) * SAMPLE_RATE / CRF_BASE_FREQUENCY;
            uint32_t pkts_per_crf = (static_cast<uint32_t>(config_.crf_timestamps_per_packet) * sample_stride) /
                static_cast<uint32_t>(SAMPLES_PER_PACKET);
            if (pkts_per_crf == 0) {
                pkts_per_crf = 1;
            }
            if (crf_decim_ == 0) {
                transmit_crf();
            }
            crf_decim_ = static_cast<uint16_t>((crf_decim_ + 1U) % pkts_per_crf);
        } else {
            crf_decim_ = 0;  // gate closed: next emission starts a fresh PDU phase
        }
    }
}

void AvbEntityAudioIO::update_gps_ratio(uint64_t gptp_now_ns)
{
    // Sample CLOCK_REALTIME (GPS, disciplined by chrony <- TM2000B) against gPTP
    // a few times per second; the ratio Kalman turns the offset slope into
    // r = switch/GPS, which pins the media-clock rate. Rate-limited so the
    // CLOCK_REALTIME syscall is off the per-packet hot path.
    constexpr uint64_t SAMPLE_INTERVAL_NS = 250'000'000;  // 0.25 s
    if (last_ratio_gptp_ns_ != 0 && (gptp_now_ns - last_ratio_gptp_ns_) < SAMPLE_INTERVAL_NS) {
        return;
    }
    timespec ts{};
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return;
    }
    uint64_t const gps_ns = (static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL) + static_cast<uint64_t>(ts.tv_nsec);
    auto const offset = static_cast<int64_t>(gptp_now_ns) - static_cast<int64_t>(gps_ns);
    double const dt = (last_ratio_gptp_ns_ != 0) ? static_cast<double>(gptp_now_ns - last_ratio_gptp_ns_) * 1e-9 : 0.0;
    gps_ratio_.add(offset, dt);
    // Feed the same (gPTP master, CLOCK_REALTIME) pair to the inter-site tunnel's
    // TAI translator so it can map the gPTP PHC -> absolute GPS-TAI (drift-free,
    // cross-site-common) for the tunnel timeline. See tai_translator_.
    tai_translator_.add_sample(static_cast<int64_t>(gptp_now_ns), static_cast<int64_t>(gps_ns));
    if (auto const est = gps_ratio_.estimate(); est.valid) {
        r_ = est.r;

        // Live media-clock telemetry (rate-limited; runs on the media-timer
        // thread, so keep it to once every few seconds). Lets an operator SEE the
        // GPS-vs-switch frequency tracking and catch a clock-coupling mistake:
        //   - r is switch_rate/GPS_rate; offset-slope = (r-1) in ppm = the
        //     fractional frequency between the gPTP PHC and GPS CLOCK_REALTIME.
        //   - PHC-REALTIME is the raw instantaneous phase offset (ns).
        // A HEALTHY decoupled setup (ptp4l -> /dev/ptp0 <- switch GM; chrony ->
        // CLOCK_REALTIME <- local GPS; NO phc2sys) shows a small but NON-ZERO,
        // stable slope -- the switch GM's free-running oscillator drifting against
        // GPS. A slope pinned at ~0.000 ppm with a flat offset means the PHC and
        // CLOCK_REALTIME are coupled (e.g. phc2sys is running) and the media clock
        // is NOT actually GPS-locked -- see umbrella docs/MEDIA_CLOCK_TIMING.md.
        constexpr uint64_t LOG_INTERVAL_NS = 5'000'000'000;  // 5 s
        if (last_ratio_log_ns_ == 0 || (gptp_now_ns - last_ratio_log_ns_) >= LOG_INTERVAL_NS) {
            std::print(
                stderr,
                "[media-clock] r(switch/GPS)={:.9f}  offset-slope={:+.3f} ppm  PHC-REALTIME={} ns  unc=+/-{:.1f} ppb\n",
                est.r,
                est.ppm(),
                static_cast<long long>(est.filtered_offset_ns),
                est.freq_uncertainty_ppb);
            last_ratio_log_ns_ = gptp_now_ns;
        }
    }
    last_ratio_gptp_ns_ = gptp_now_ns;
}

auto AvbEntityAudioIO::talker_should_transmit(uint16_t const idx, int64_t const now_ns) const noexcept -> bool
{
    if (!config_.gate_talker_on_listener) {
        return true;
    }
    // Spec-correct gate: transmit ONLY when BOTH an ACMP connection exists AND the
    // listener permits transmit via MSRP Listener Ready. A talker must not put a
    // stream on the SR class until the reservation is in place.
    auto const* s = components_.acmp_talker.get_stream(idx);
    bool const acmp = s != nullptr && s->connection_count() > 0;
    if (!acmp) {
        return false;
    }
    if (idx >= msrp_ready_ns_.size()) {
        return acmp;
    }
    // MSRP Listener Ready, held across the MRP LeaveAll re-registration blip by a
    // grace window: the peer's Listener declaration ages out and re-declares on a
    // ~10s leave-all cycle (Ready momentarily withdrawn for ~1-3s), which must NOT
    // chop the stream. Stay up while Ready is currently set OR was set within
    // GRACE. A genuine listener departure (no re-declare for GRACE) closes the gate.
    constexpr int64_t GRACE_NS = 5'000'000'000;  // 5 s -- comfortably covers a LeaveAll cycle
    int64_t const last_ready = msrp_ready_ns_[idx].load(std::memory_order_relaxed);
    bool const msrp =
        msrp_listener_ready_[idx].load(std::memory_order_relaxed) || (last_ready != 0 && (now_ns - last_ready) < GRACE_NS);
    return msrp;
}

void AvbEntityAudioIO::transmit_am824(uint64_t now_ns, uint32_t samples)
{
    if (!am824_out_ || stream_tx_.fd() < 0) {
        return;
    }
    static constexpr size_t MAX_FRAME =
        avtp::Am824Pdu::HEADER_LENGTH + (avtp::Am824Pdu::MAX_SAMPLES_PER_PACKET * avtp::Am824Pdu::MAX_CHANNELS * 4);
    std::array<uint8_t, MAX_FRAME> frame{};

    avtp::Am824Pdu pdu{};
    pdu.init(am824_out_->stream_id, static_cast<uint8_t>(channels_), avtp::Am824SampleRate::rate_96_khz);

    std::span<uint8_t> const payload = std::span<uint8_t>{frame}.subspan(avtp::Am824Pdu::HEADER_LENGTH);
    size_t const audio_bytes = avtp::am824_serialize_mbla(
        *am824_out_, pdu, payload, static_cast<uint8_t>(samples), now_ns, [this](uint8_t ch, std::span<float> dest) {
            for (size_t s = 0; s < dest.size(); ++s) {
                dest[s] = audio_buffer_[(s * channels_) + ch];
            }
        });
    if (audio_bytes == 0) {
        return;
    }

    span_store(std::span<uint8_t>{frame}.first(avtp::Am824Pdu::HEADER_LENGTH), pdu);
    size_t const frame_len = avtp::Am824Pdu::HEADER_LENGTH + audio_bytes;
    // AVB stream frames must be VLAN-tagged with the SR class VID + PCP so bridges
    // admit them to the reserved SR class (untagged AVTP is not part of any SR class).
    // pcap stamp = the gPTP WALL-CLOCK transmit time (not now_ns, which is the
    // media-clock PRESENTATION timestamp also written into the AVTP header). Using
    // the wall clock lets a capture reveal the real presentation lead
    // (avtp_ts - wall_clock = presentation_offset).
    last_tx_gptp_ns_ = last_gptp_ns_.load(std::memory_order_relaxed);
    (void)stream_tx_.send_vlan(
        &am824_dest_mac_, std::span<uint8_t const>{frame.data(), frame_len}, config_.vlan_id, config_.stream_pcp);
    ++am824_tx_packets_;
}

void AvbEntityAudioIO::transmit_aaf(uint64_t now_ns, uint16_t samples, std::span<float const> src)
{
    if (!aaf_out_ || stream_tx_.fd() < 0) {
        return;
    }
    static constexpr size_t MAX_FRAME =
        avtp::AafPdu::HEADER_LENGTH + (avtp::Am824Pdu::MAX_SAMPLES_PER_PACKET * avtp::Am824Pdu::MAX_CHANNELS * 4);
    std::array<uint8_t, MAX_FRAME> frame{};

    avtp::AafPdu pdu{};
    std::span<uint8_t> const payload = std::span<uint8_t>{frame}.subspan(avtp::AafPdu::HEADER_LENGTH);
    size_t const audio_bytes =
        avtp::aaf_stream_serialize(*aaf_out_, pdu, payload, samples, now_ns, [this, src](uint8_t ch, std::span<float> dest) {
            for (size_t s = 0; s < dest.size(); ++s) {
                dest[s] = src[(s * channels_) + ch];
            }
        });
    if (audio_bytes == 0) {
        return;
    }

    span_store(std::span<uint8_t>{frame}.first(avtp::AafPdu::HEADER_LENGTH), pdu);
    size_t const frame_len = avtp::AafPdu::HEADER_LENGTH + audio_bytes;
    // pcap stamp = gPTP WALL-CLOCK transmit time (see transmit_am824); now_ns here
    // is the media-clock PRESENTATION time, also written into the AVTP header.
    last_tx_gptp_ns_ = last_gptp_ns_.load(std::memory_order_relaxed);
    (void)stream_tx_.send_vlan(
        &aaf_dest_mac_, std::span<uint8_t const>{frame.data(), frame_len}, config_.vlan_id, config_.stream_pcp);
    ++aaf_tx_packets_;
}

void AvbEntityAudioIO::transmit_crf()
{
    if (!crf_out_ || stream_tx_.fd() < 0 || !media_clock_.anchored()) {
        return;
    }
    uint16_t const n_ts = config_.crf_timestamps_per_packet;
    uint16_t const interval = config_.crf_timestamp_interval;
    // Convert the DECLARED interval (in CRF base-frequency events) into our audio
    // SAMPLE_RATE (96 kHz) sample-index domain that media_clock_.timestamp_for()
    // speaks. With a 48 kHz CRF base and 96 kHz audio, each declared CRF event spans
    // SAMPLE_RATE/base (= 2) audio samples, so the emitted timestamp VALUES stay
    // spaced at interval/base seconds regardless of the base we advertise.
    uint64_t const sample_stride = static_cast<uint64_t>(interval) * SAMPLE_RATE / crf_out_->base_frequency;

    static constexpr size_t MAX_FRAME = avtp::CrfPdu::HEADER_LENGTH + (64 * avtp::CrfPdu::TIMESTAMP_SIZE);
    std::array<uint8_t, MAX_FRAME> frame{};

    avtp::CrfPdu pdu{};
    pdu.init_audio_sample(crf_out_->stream_id, crf_out_->base_frequency, crf_out_->pull, interval, n_ts);
    pdu.set_sequence_num(crf_out_->sequence_num);

    // Timestamps come from the SAME GPS-locked media clock as the audio, so the CRF
    // conveys exactly our media clock (event times, evenly spaced by sample_stride
    // in the 96 kHz domain). timestamp_for() already carries the presentation
    // offset (Open1722's CRF talker likewise offsets by the max transit time).
    std::span<uint8_t> const ts_data = std::span<uint8_t>{frame}.subspan(avtp::CrfPdu::HEADER_LENGTH);
    for (uint16_t i = 0; i < n_ts; ++i) {
        uint64_t const ts = media_clock_.timestamp_for(crf_event_ + (static_cast<uint64_t>(i) * sample_stride));
        (void)avtp::crf_set_timestamp(ts_data, i, ts);
    }
    crf_event_ += static_cast<uint64_t>(n_ts) * sample_stride;
    crf_out_->sequence_num = static_cast<uint8_t>((crf_out_->sequence_num + 1U) & 0xFFU);
    ++crf_out_->packets_sent;

    span_store(std::span<uint8_t>{frame}.first(avtp::CrfPdu::HEADER_LENGTH), pdu);
    size_t const frame_len = avtp::CrfPdu::HEADER_LENGTH + (static_cast<size_t>(n_ts) * avtp::CrfPdu::TIMESTAMP_SIZE);
    last_tx_gptp_ns_ = last_gptp_ns_.load(std::memory_order_relaxed);  // gPTP timestamp for the optional TX pcap tap
    (void)stream_tx_.send_vlan(
        &crf_dest_mac_, std::span<uint8_t const>{frame.data(), frame_len}, config_.vlan_id, config_.stream_pcp);
}

void AvbEntityAudioIO::on_stream_rx_frame(std::span<uint8_t const> frame, int64_t now_ns)
{
    if (frame.empty()) {
        return;
    }

    if (frame[0] == avtp::AvtpSubtype::iec_61883_iidc) {
        // --- AM824 (stream 0) ---
        if (!am824_in_ || frame.size() < avtp::Am824Pdu::HEADER_LENGTH) {
            return;
        }
        // Only ingest the stream connected to this input; the promiscuous socket
        // also sees our own TX and any other AVB stream on the segment.
        if (!frame_is_for_listener(AM824_STREAM_INDEX, frame)) {
            return;
        }
        avtp::Am824Pdu pdu{};
        span_load(pdu, frame.first(avtp::Am824Pdu::HEADER_LENGTH));
        if (!pdu.is_valid()) {
            am824_rx_bad_.fetch_add(1, std::memory_order_relaxed);
            update_stream_input_counters(AM824_STREAM_INDEX, 0, 0, false, false, false, /*format_ok=*/false, 0);
            return;
        }
        std::span<uint8_t const> const audio = frame.subspan(avtp::Am824Pdu::HEADER_LENGTH);
        uint64_t samples_this = 0;
        avtp::am824_deserialize_mbla(
            *am824_in_,
            pdu,
            audio,
            static_cast<uint64_t>(now_ns),
            [&samples_this](uint8_t /*ch*/, std::span<float> s, uint64_t /*pts*/, uint64_t /*period*/) {
                samples_this = s.size();
            });
        am824_rx_packets_.fetch_add(1, std::memory_order_relaxed);
        am824_rx_samples_.fetch_add(samples_this, std::memory_order_relaxed);
        update_stream_input_counters(
            AM824_STREAM_INDEX,
            pdu.stream_header.sequence_num.get(),
            pdu.avtp_timestamp(),
            pdu.stream_header.tv(),
            pdu.stream_header.tu(),
            pdu.stream_header.mr(),
            /*format_ok=*/true,
            samples_this);

        // Inter-site UDPTUN ingest: forward this AM824 audio when AM824 is the
        // configured tunnel source. The tunnel transport + far egress are AAF
        // int32, so the MBLA quadlets MUST be transcoded to int32 first -- sending
        // the raw [0x40 label][24-bit] bytes makes the far end read the label as
        // the sample MSB (+0.5 FS DC pedestal + crushed audio = distortion).
        if (config_.udptun_source_stream == AM824_STREAM_INDEX && !config_.sweep_enable) {
            udptun_ingest_am824_as_int32(audio);
        }
    } else if (frame[0] == avtp::AvtpSubtype::aaf) {
        // --- AAF (stream 1) ---
        if (!aaf_in_) {
            return;
        }
        if (!frame_is_for_listener(AAF_STREAM_INDEX, frame)) {
            return;
        }
        auto pdu_opt = avtp::aaf_parse_header(frame);
        if (!pdu_opt) {
            aaf_rx_bad_.fetch_add(1, std::memory_order_relaxed);
            update_stream_input_counters(AAF_STREAM_INDEX, 0, 0, false, false, false, /*format_ok=*/false, 0);
            return;
        }
        std::span<uint8_t const> const audio = avtp::aaf_get_audio_payload(frame);
        uint64_t samples_this = 0;
        avtp::aaf_stream_deserialize(
            *aaf_in_,
            *pdu_opt,
            audio,
            static_cast<uint64_t>(now_ns),
            [&samples_this](uint8_t /*ch*/, std::span<float> s, uint64_t /*pts*/, uint64_t /*period*/) {
                samples_this = s.size();
            });
        aaf_rx_packets_.fetch_add(1, std::memory_order_relaxed);
        aaf_rx_samples_.fetch_add(samples_this, std::memory_order_relaxed);
        update_stream_input_counters(
            AAF_STREAM_INDEX,
            pdu_opt->get_sequence_num(),
            pdu_opt->get_avtp_timestamp(),
            pdu_opt->tv(),
            pdu_opt->tu(),
            pdu_opt->mr(),
            /*format_ok=*/true,
            samples_this);

        // Inter-site UDPTUN ingest: forward this AAF (interleaved int32) audio to
        // the far site when AAF is the configured tunnel source.
        if (config_.udptun_source_stream == AAF_STREAM_INDEX && !config_.sweep_enable) {
            udptun_ingest_audio(audio);
        }
    }
}

void AvbEntityAudioIO::udptun_ingest_am824_as_int32(std::span<uint8_t const> const mbla)
{
    // AM824 MBLA data block = one 32-bit quadlet per sample: [label:8][audio:24]
    // big-endian. The inter-site tunnel + the far egress are AAF int32, so emit a
    // genuine int32 sample = the 24-bit audio MSB-aligned with a zero low byte
    // ([b1][b2][b3][0x00] = audio << 8). Dropping the label is exactly what makes
    // the far end NOT read 0x40 as the sample's MSB. Lossless for 24-bit audio.
    if (!udptun_enable_) {
        return;
    }
    size_t const quads = mbla.size() / 4;
    size_t const need = quads * 4;
    if (udptun_am824_transcode_buf_.size() < need) {
        udptun_am824_transcode_buf_.resize(need);  // grows once; steady-state no alloc
    }
    uint8_t* const out = udptun_am824_transcode_buf_.data();
    for (size_t q = 0; q < quads; ++q) {
        size_t const i = q * 4;
        out[i + 0] = mbla[i + 1];  // audio[23:16]
        out[i + 1] = mbla[i + 2];  // audio[15:8]
        out[i + 2] = mbla[i + 3];  // audio[7:0]
        out[i + 3] = 0x00;         // int32 low byte (24-bit MSB-aligned)
    }
    udptun_ingest_audio(std::span<uint8_t const>{out, need});
}

void AvbEntityAudioIO::udptun_ingest_audio(std::span<uint8_t const> const audio, bool const real_source)
{
    // `audio` is the raw network-order payload of the received stream packet
    // (4-byte samples: AAF int32, or AM824 24-in-32). It is carried opaquely by
    // the tunnel and re-emitted at the far end, so there is no float round-trip.
    // TAI is anchored to CLOCK_REALTIME + offset on the first packet, then the
    // ingest advances it by exact frame duration (drift-free).
    if (!udptun_enable_ || !udptun_ingest_) {
        return;
    }
    size_t const frame_bytes = static_cast<size_t>(channels_) * 4;
    if (frame_bytes == 0 || audio.size() < frame_bytes) {
        return;
    }
    auto const n_frames = static_cast<uint16_t>(audio.size() / frame_bytes);
    // Mark that real tunnel audio is flowing so the punch keepalive AND the
    // silence-source gate stand down (the audio itself keeps the pinhole open and
    // is the single producer into the ingest). ONLY real listener audio stamps
    // this -- the silence filler must not, or the gate would read its own silence
    // as "real audio" and suppress itself. CLOCK_REALTIME = same TAI base the media
    // thread's checks read.
    if (real_source) {
        timespec rts{};
        if (clock_gettime(CLOCK_REALTIME, &rts) == 0) {
            udptun_last_real_ingest_tai_.store(
                (static_cast<int64_t>(rts.tv_sec) * 1'000'000'000LL) + rts.tv_nsec + config_.udptun_tai_offset_ns,
                std::memory_order_relaxed);
        }
    }
    if (!udptun_anchored_) {
        // Anchor the ingest timeline to GPS-TAI via the translator (gPTP master ->
        // GPS-TAI), the same clock that paces the source and that the egress plays
        // on -- so the ingest avtp_timestamp is drift-free TAI. Fall back to raw
        // CLOCK_REALTIME+offset until the translator has a sample.
        int64_t anchor_tai_ns = 0;
        if (tai_translator_.has_sample()) {
            anchor_tai_ns = tai_translator_.tai_ns(static_cast<int64_t>(last_gptp_ns_.load(std::memory_order_relaxed)));
        } else {
            timespec ts{};
            if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
                return;
            }
            anchor_tai_ns = (static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL) + ts.tv_nsec + config_.udptun_tai_offset_ns;
        }
        udptun_ingest_->start(anchor_tai_ns);
        udptun_anchored_ = true;
    }
    // Discipline the free-running ingest TAI to the live GPS-TAI. The ingest counts
    // frames at the nominal sample rate, which only tracks real time when the SOURCE
    // media clock is GPS-locked. The the audio interface loopback follows the local switch gPTP
    // (not a GPS CRF), ~172 ppm off GPS, so without this the presentation time slides
    // ~500 ms/hour out of the far egress window (the AAF path stays locked because the DSP processor
    // follows our GPS-rate CRF). The slew is gentle, so the emitted timestamps stay
    // smooth. Only when the translator has a global-epoch sample.
    if (tai_translator_.has_sample()) {
        udptun_ingest_->discipline(tai_translator_.tai_ns(static_cast<int64_t>(last_gptp_ns_.load(std::memory_order_relaxed))));
    }
    (void)udptun_ingest_->submit(audio.first(static_cast<size_t>(n_frames) * frame_bytes), n_frames, [this](auto const& pkt) {
        udptun_send(pkt.tai_ns, pkt.pcm);
    });
}

auto AvbEntityAudioIO::frame_is_for_listener(uint16_t const stream_index, std::span<uint8_t const> frame) const -> bool
{
    // AVTP stream_id is 8 bytes at offset 4 (subtype@0, sv/ver/flags@1, seq@2,
    // ...). The VLAN tag is already stripped by RawnetContext::recv.
    if (frame.size() < 12) {
        return false;
    }
    auto const* ls = components_.acmp_listener.get_stream(stream_index);
    if (ls == nullptr || !ls->connected) {
        return false;
    }
    auto const id_bytes = make_const_span(ls->stream_id);  // 8 bytes
    return std::equal(id_bytes.begin(), id_bytes.end(), frame.begin() + 4);
}

void AvbEntityAudioIO::update_stream_input_counters(
    uint16_t const stream_index,
    uint8_t const seq,
    uint32_t const avtp_ts,
    bool const tv,
    bool const tu,
    bool const mr,
    bool const format_ok,
    uint64_t const samples_per_ch)
{
    if (stream_index >= stream_in_counters_.size()) {
        return;
    }
    auto& c = stream_in_counters_[stream_index];
    auto const relaxed = std::memory_order_relaxed;
    c.frames_rx.fetch_add(1, relaxed);
    if (!format_ok) {
        c.unsupported_format.fetch_add(1, relaxed);
        return;
    }

    // Sequence-number continuity (8-bit wrap).
    if (c.have_prev && seq != static_cast<uint8_t>(c.prev_seq + 1U)) {
        c.seq_num_mismatch.fetch_add(1, relaxed);
    }
    // Media clock restart: the talker toggles mr when its media clock resets.
    if (c.have_prev && mr != c.prev_mr) {
        c.media_reset.fetch_add(1, relaxed);
    }

    if (tv) {
        c.timestamp_valid.fetch_add(1, relaxed);
        if (tu) {
            c.timestamp_uncertain.fetch_add(1, relaxed);
        }
        // LATE/EARLY vs gPTP-now (lower 32 bits of ns). The reactor clock is
        // monotonic, so use last_gptp_ns_ published by the media timer (<=125us
        // stale, fine against ms-scale thresholds). diff<0 => presentation time
        // already passed (late); diff implausibly large => stamped too early.
        constexpr uint32_t EARLY_THRESHOLD_NS = 50'000'000;  // 50 ms
        uint64_t const gptp_now = last_gptp_ns_.load(relaxed);
        if (gptp_now != 0) {
            int32_t const diff = static_cast<int32_t>(avtp_ts - static_cast<uint32_t>(gptp_now));
            if (diff < 0) {
                c.late_timestamp.fetch_add(1, relaxed);
            } else if (static_cast<uint32_t>(diff) > EARLY_THRESHOLD_NS) {
                c.early_timestamp.fetch_add(1, relaxed);
            }
        }
        // Media lock = a STEADY inter-(valid-)timestamp STEP. AAF advances
        // 125 us/packet (12 samples @ 96 kHz). AM824's avtp_timestamp instead
        // follows the IEC 61883-6 SYT cadence: SYT_INTERVAL=16 @ 96 kHz, so a valid
        // timestamp appears on 3 of every 4 (12-sample) packets and the valid stamps
        // step a constant 166.67 us. So we lock on the step being CONSTANT, not on a
        // hardcoded per-packet value (which only ever matched AAF). Both ends are
        // gPTP-slaved so the step is correct by construction; jitter/drops change it
        // and unlock. A coarse plausibility window vs the nominal packet period
        // rejects garbage so two equal junk steps cannot false-lock.
        uint32_t const lock_tolerance_ns = config_.lock_tolerance_ns;  // +/-, default 5 us, CLI-tunable
        constexpr int LOCK_RUN_THRESHOLD = 8;                          // ~1 ms (AAF) / ~1.3 ms (AM824) of clean steps
        if (c.have_prev_ts && samples_per_ch > 0) {
            uint32_t const actual = avtp_ts - c.prev_ts;                                 // wrap-safe (uint32)
            uint64_t const nominal = (samples_per_ch * 1'000'000'000ULL) / SAMPLE_RATE;  // 125 us @ 12/96k
            bool const plausible = (actual >= (nominal / 2)) && (actual < (nominal * 4));
            uint64_t const step_err = (actual > c.prev_delta) ? (actual - c.prev_delta) : (c.prev_delta - actual);
            if (plausible && c.have_prev_delta && step_err <= lock_tolerance_ns) {
                if (c.locked_run < LOCK_RUN_THRESHOLD) {
                    ++c.locked_run;
                }
                if (c.locked_run >= LOCK_RUN_THRESHOLD && !c.is_locked) {
                    c.is_locked = true;
                    c.media_locked.fetch_add(1, relaxed);
                }
            } else {
                c.locked_run = 0;
                if (c.is_locked) {
                    c.is_locked = false;
                    c.media_unlocked.fetch_add(1, relaxed);
                }
            }
            c.prev_delta = actual;
            c.have_prev_delta = true;
        }
        c.prev_ts = avtp_ts;
        c.have_prev_ts = true;
    } else {
        c.timestamp_not_valid.fetch_add(1, relaxed);
    }

    c.prev_seq = seq;
    c.prev_mr = mr;
    c.have_prev = true;
}

auto AvbEntityAudioIO::fill_stream_input_counters(
    uint16_t const descriptor_index, uint32_t& valid, std::array<uint32_t, 32>& out) const -> bool
{
    if (descriptor_index >= stream_in_counters_.size()) {
        return false;
    }
    auto const& c = stream_in_counters_[descriptor_index];
    auto const relaxed = std::memory_order_relaxed;
    // IEEE 1722.1 STREAM_INPUT counter bit positions (Clause 7.4.42). counters[bit]
    // holds the value for the bit set in `valid`.
    auto set = [&](size_t bit, uint32_t v) {
        valid |= (1U << bit);
        out[bit] = v;
    };
    set(0, c.media_locked.load(relaxed));
    set(1, c.media_unlocked.load(relaxed));
    set(3, c.seq_num_mismatch.load(relaxed));
    set(4, c.media_reset.load(relaxed));
    set(5, c.timestamp_uncertain.load(relaxed));
    set(6, c.timestamp_valid.load(relaxed));
    set(7, c.timestamp_not_valid.load(relaxed));
    set(8, c.unsupported_format.load(relaxed));
    set(9, c.late_timestamp.load(relaxed));
    set(10, c.early_timestamp.load(relaxed));
    set(11, c.frames_rx.load(relaxed));
    return true;
}

auto AvbEntityAudioIO::fill_stream_output_counters(
    uint16_t const descriptor_index, uint32_t& valid, std::array<uint32_t, 32>& out) const -> bool
{
    // Our talker (STREAM_OUTPUT) descriptors: 0=AM824, 1=AAF, 2=CRF. Each maps to
    // its on-wire TX packet counter (tx counters are single-threaded with the
    // media timer, read plain like print_state()).
    uint64_t frames_tx = 0;
    switch (descriptor_index) {
        case AM824_STREAM_INDEX:
            frames_tx = am824_tx_packets_;
            break;
        case AAF_STREAM_INDEX:
            frames_tx = aaf_tx_packets_;
            break;
        case CRF_STREAM_INDEX:
            frames_tx = crf_out_ ? crf_out_->packets_sent : 0;
            break;
        default:
            return false;
    }
    // IEEE 1722.1 STREAM_OUTPUT counter bit positions (Clause 7.4.43). We expose
    // FRAMES_TX (bit 6) -- total media frames this talker has put on the wire --
    // which is what tells a reader our actual transmit rate.
    valid |= (1U << 6U);
    out[6] = static_cast<uint32_t>(frames_tx);
    return true;
}

auto AvbEntityAudioIO::fill_stream_output_info(
    uint16_t const descriptor_type, uint16_t const descriptor_index, atdecc::aem::AemStreamInfoPayload& out) const -> bool
{
    // Only our talker (STREAM_OUTPUT) streams carry GET_STREAM_INFO here.
    if (descriptor_type != DESCRIPTOR_STREAM_OUTPUT) {
        return false;
    }
    auto const* stream = components_.acmp_talker.get_stream(descriptor_index);
    if (stream == nullptr) {
        return false;
    }

    uint32_t flags = stream_info_flags::STREAM_ID_VALID | stream_info_flags::STREAM_DEST_MAC_VALID |
        stream_info_flags::STREAM_VLAN_ID_VALID | stream_info_flags::MSRP_ACC_LAT_VALID;

    // Stream format from the STREAM_OUTPUT descriptor's current_format (8 bytes).
    if (auto const fmt = components_.entity_model.get_stream_output(descriptor_index); fmt.has_value() && fmt.value() != nullptr) {
        auto const fspan = fmt.value()->current_format.span();
        std::copy(fspan.begin(), fspan.end(), out.stream_format.begin());
        flags |= stream_info_flags::STREAM_FORMAT_VALID;
    }

    out.stream_id = stream->stream_id;
    auto const mspan = stream->stream_dest_mac.span();
    std::copy(mspan.begin(), mspan.end(), out.stream_dest_mac.begin());
    out.stream_vlan_id = ieee::doublet_t{stream->stream_vlan_id};
    out.msrp_accumulated_latency = ieee::quadlet_t{static_cast<uint32_t>(config_.presentation_offset_ns)};

    // SR class A is the entity's only class, so CLASS_B stays clear. Report the
    // live ACMP connection state so a controller/listener sees CONNECTED.
    if (components_.acmp_talker.connection_count(descriptor_index) > 0) {
        flags |= stream_info_flags::CONNECTED;
    }
    out.flags = ieee::quadlet_t{flags};
    return true;
}

void AvbEntityAudioIO::configure_filter(double const freq_hz, double const gain_db, double const q)
{
    double const sample_rate_recip = 1.0 / static_cast<double>(SAMPLE_RATE);
    for (size_t ch = 0; ch < channels_; ++ch) {
        biquads_[ch].coeffs.calculate_peak(
            {.sample_rate_recip = sample_rate_recip, .frequency = freq_hz, .q = q, .gain_db = gain_db, .channel = 0});
        biquads_[ch].reset();
    }
}

}  // namespace statusbar::avb_entity
