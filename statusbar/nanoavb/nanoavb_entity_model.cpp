// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/nanoavb/nanoavb_entity_model.hpp"

namespace statusbar::nanoavb {

// Template helpers to avoid repeating the same get/add pattern for every descriptor type.

namespace {

template <typename DescT, typename Container>
auto get_desc(Container const& c, uint16_t index) noexcept -> StatusValue<DescT const*>
{
    if (index >= c.size()) {
        return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
    }
    return &c[index];
}

template <typename DescT, typename Container>
auto add_desc(Container& c, size_t max_size, DescT const& desc) -> StatusValue<uint16_t>
{
    if (c.size() >= max_size) {
        return failure(make_error_code(NanoAvbError::DescriptorStorageFull));
    }
    auto index = static_cast<uint16_t>(c.size());
    c.push_back(desc);
    c.back().descriptor_index = index;
    return index;
}

template <typename DescT, typename Container>
auto add_desc_typed(Container& c, size_t max_size, DescT const& desc, uint16_t descriptor_type) -> StatusValue<uint16_t>
{
    if (c.size() >= max_size) {
        return failure(make_error_code(NanoAvbError::DescriptorStorageFull));
    }
    auto index = static_cast<uint16_t>(c.size());
    c.push_back(desc);
    c.back().descriptor_type = descriptor_type;
    c.back().descriptor_index = index;
    return index;
}

}  // namespace

// Typed descriptor accessors — each delegates to the template helpers above.

auto EntityModel::get_configuration(uint16_t index) const noexcept -> StatusValue<DescriptorConfiguration const*>
{
    return get_desc<DescriptorConfiguration>(configurations_, index);
}
auto EntityModel::get_audio_unit(uint16_t index) const noexcept -> StatusValue<DescriptorAudioUnit const*>
{
    return get_desc<DescriptorAudioUnit>(audio_units_, index);
}
auto EntityModel::get_stream_input(uint16_t index) const noexcept -> StatusValue<DescriptorStream const*>
{
    return get_desc<DescriptorStream>(stream_inputs_, index);
}
auto EntityModel::get_stream_output(uint16_t index) const noexcept -> StatusValue<DescriptorStream const*>
{
    return get_desc<DescriptorStream>(stream_outputs_, index);
}
auto EntityModel::get_jack_input(uint16_t index) const noexcept -> StatusValue<DescriptorJack const*>
{
    return get_desc<DescriptorJack>(jack_inputs_, index);
}
auto EntityModel::get_jack_output(uint16_t index) const noexcept -> StatusValue<DescriptorJack const*>
{
    return get_desc<DescriptorJack>(jack_outputs_, index);
}
auto EntityModel::get_avb_interface(uint16_t index) const noexcept -> StatusValue<DescriptorAvbInterface const*>
{
    return get_desc<DescriptorAvbInterface>(avb_interfaces_, index);
}
auto EntityModel::get_clock_source(uint16_t index) const noexcept -> StatusValue<DescriptorClockSource const*>
{
    return get_desc<DescriptorClockSource>(clock_sources_, index);
}
auto EntityModel::get_clock_domain(uint16_t index) const noexcept -> StatusValue<DescriptorClockDomain const*>
{
    return get_desc<DescriptorClockDomain>(clock_domains_, index);
}
auto EntityModel::get_locale(uint16_t index) const noexcept -> StatusValue<DescriptorLocale const*>
{
    return get_desc<DescriptorLocale>(locales_, index);
}
auto EntityModel::get_strings(uint16_t index) const noexcept -> StatusValue<DescriptorStrings const*>
{
    return get_desc<DescriptorStrings>(strings_, index);
}
auto EntityModel::get_stream_port_input(uint16_t index) const noexcept -> StatusValue<DescriptorStreamPort const*>
{
    return get_desc<DescriptorStreamPort>(stream_port_inputs_, index);
}
auto EntityModel::get_stream_port_output(uint16_t index) const noexcept -> StatusValue<DescriptorStreamPort const*>
{
    return get_desc<DescriptorStreamPort>(stream_port_outputs_, index);
}
auto EntityModel::get_audio_cluster(uint16_t index) const noexcept -> StatusValue<DescriptorAudioCluster const*>
{
    return get_desc<DescriptorAudioCluster>(audio_clusters_, index);
}
auto EntityModel::get_audio_map(uint16_t index) const noexcept -> StatusValue<DescriptorAudioMap const*>
{
    return get_desc<DescriptorAudioMap>(audio_maps_, index);
}
auto EntityModel::get_control(uint16_t index) const noexcept -> StatusValue<DescriptorControl const*>
{
    return get_desc<DescriptorControl>(controls_, index);
}

// Simple add (no descriptor_type override)
auto EntityModel::add_configuration(DescriptorConfiguration const& desc) -> StatusValue<uint16_t>
{
    return add_desc(configurations_, config_.max_configurations, desc);
}
auto EntityModel::add_audio_unit(DescriptorAudioUnit const& desc) -> StatusValue<uint16_t>
{
    return add_desc(audio_units_, config_.max_audio_units, desc);
}
auto EntityModel::add_avb_interface(DescriptorAvbInterface const& desc) -> StatusValue<uint16_t>
{
    return add_desc(avb_interfaces_, config_.max_avb_interfaces, desc);
}
auto EntityModel::add_clock_source(DescriptorClockSource const& desc) -> StatusValue<uint16_t>
{
    return add_desc(clock_sources_, config_.max_clock_sources, desc);
}
auto EntityModel::add_clock_domain(DescriptorClockDomain const& desc) -> StatusValue<uint16_t>
{
    return add_desc(clock_domains_, config_.max_clock_domains, desc);
}
auto EntityModel::add_locale(DescriptorLocale const& desc) -> StatusValue<uint16_t>
{
    return add_desc(locales_, config_.max_locales, desc);
}
auto EntityModel::add_strings(DescriptorStrings const& desc) -> StatusValue<uint16_t>
{
    return add_desc(strings_, config_.max_strings, desc);
}
auto EntityModel::add_audio_cluster(DescriptorAudioCluster const& desc) -> StatusValue<uint16_t>
{
    return add_desc(audio_clusters_, config_.max_audio_clusters, desc);
}
auto EntityModel::add_audio_map(DescriptorAudioMap const& desc) -> StatusValue<uint16_t>
{
    return add_desc(audio_maps_, config_.max_audio_maps, desc);
}
auto EntityModel::add_control(DescriptorControl const& desc) -> StatusValue<uint16_t>
{
    return add_desc(controls_, config_.max_controls, desc);
}

// Typed add (sets descriptor_type on the stored element)
auto EntityModel::add_stream_input(DescriptorStream const& desc) -> StatusValue<uint16_t>
{
    return add_desc_typed(stream_inputs_, config_.max_stream_inputs, desc, DESCRIPTOR_STREAM_INPUT);
}
auto EntityModel::add_stream_output(DescriptorStream const& desc) -> StatusValue<uint16_t>
{
    return add_desc_typed(stream_outputs_, config_.max_stream_outputs, desc, DESCRIPTOR_STREAM_OUTPUT);
}
auto EntityModel::add_jack_input(DescriptorJack const& desc) -> StatusValue<uint16_t>
{
    return add_desc_typed(jack_inputs_, config_.max_jack_inputs, desc, DESCRIPTOR_JACK_INPUT);
}
auto EntityModel::add_jack_output(DescriptorJack const& desc) -> StatusValue<uint16_t>
{
    return add_desc_typed(jack_outputs_, config_.max_jack_outputs, desc, DESCRIPTOR_JACK_OUTPUT);
}
auto EntityModel::add_stream_port_input(DescriptorStreamPort const& desc) -> StatusValue<uint16_t>
{
    return add_desc_typed(stream_port_inputs_, config_.max_stream_port_inputs, desc, DESCRIPTOR_STREAM_PORT_INPUT);
}
auto EntityModel::add_stream_port_output(DescriptorStreamPort const& desc) -> StatusValue<uint16_t>
{
    return add_desc_typed(stream_port_outputs_, config_.max_stream_port_outputs, desc, DESCRIPTOR_STREAM_PORT_OUTPUT);
}

// Generic Descriptor Access (by type and index)

auto EntityModel::get_descriptor_raw(uint16_t descriptor_type, uint16_t descriptor_index) const noexcept
    -> StatusValue<std::span<uint8_t const>>
{
    // Check external DescriptorStorage blob first (if attached)
    if (descriptor_storage_.has_value()) {
        auto result = descriptor_storage_->get_descriptor(0, descriptor_type, descriptor_index);
        if (result.has_value()) {
            return result;
        }
        // Fall through to internal vectors if not found in storage
    }

    switch (descriptor_type) {
        case DESCRIPTOR_ENTITY:
            if (descriptor_index != 0) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            return make_const_span(entity_);

        case DESCRIPTOR_CONFIGURATION:
            if (descriptor_index >= configurations_.size()) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            // DescriptorConfiguration now carries an inline descriptor_counts
            // trailer; wire_span() returns the active wire_size() = header +
            // descriptor_counts_count*4 bytes.
            return wire_span(configurations_[descriptor_index]);

        case DESCRIPTOR_AUDIO_UNIT:
            if (descriptor_index >= audio_units_.size()) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            // DescriptorAudioUnit now carries an inline sampling_rates
            // trailer; wire_span() returns LENGTH + sampling_rates_count*4.
            return wire_span(audio_units_[descriptor_index]);

        case DESCRIPTOR_STREAM_INPUT:
            if (descriptor_index >= stream_inputs_.size()) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            // DescriptorStream now carries an inline stream_formats
            // trailer; wire_span() returns LENGTH + number_of_formats*8.
            return wire_span(stream_inputs_[descriptor_index]);

        case DESCRIPTOR_STREAM_OUTPUT:
            if (descriptor_index >= stream_outputs_.size()) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            return wire_span(stream_outputs_[descriptor_index]);

        case DESCRIPTOR_JACK_INPUT:
            if (descriptor_index >= jack_inputs_.size()) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            return make_const_span(jack_inputs_[descriptor_index]);

        case DESCRIPTOR_JACK_OUTPUT:
            if (descriptor_index >= jack_outputs_.size()) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            return make_const_span(jack_outputs_[descriptor_index]);

        case DESCRIPTOR_AVB_INTERFACE:
            if (descriptor_index >= avb_interfaces_.size()) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            return make_const_span(avb_interfaces_[descriptor_index]);

        case DESCRIPTOR_CLOCK_SOURCE:
            if (descriptor_index >= clock_sources_.size()) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            return make_const_span(clock_sources_[descriptor_index]);

        case DESCRIPTOR_CLOCK_DOMAIN:
            if (descriptor_index >= clock_domains_.size()) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            // DescriptorClockDomain now carries an inline clock_sources
            // trailer; wire_span() returns LENGTH + clock_sources_count*2.
            return wire_span(clock_domains_[descriptor_index]);

        case DESCRIPTOR_LOCALE:
            if (descriptor_index >= locales_.size()) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            return make_const_span(locales_[descriptor_index]);

        case DESCRIPTOR_STRINGS:
            if (descriptor_index >= strings_.size()) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            return make_const_span(strings_[descriptor_index]);

        case DESCRIPTOR_STREAM_PORT_INPUT:
            if (descriptor_index >= stream_port_inputs_.size()) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            return make_const_span(stream_port_inputs_[descriptor_index]);

        case DESCRIPTOR_STREAM_PORT_OUTPUT:
            if (descriptor_index >= stream_port_outputs_.size()) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            return make_const_span(stream_port_outputs_[descriptor_index]);

        case DESCRIPTOR_AUDIO_CLUSTER:
            if (descriptor_index >= audio_clusters_.size()) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            return make_const_span(audio_clusters_[descriptor_index]);

        case DESCRIPTOR_AUDIO_MAP:
            if (descriptor_index >= audio_maps_.size()) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            // DescriptorAudioMap now contains a max-sized inline mappings
            // trailer, so sizeof is no longer the wire size. wire_span()
            // returns exactly wire_size() = LENGTH + number_of_mappings*8.
            return wire_span(audio_maps_[descriptor_index]);

        case DESCRIPTOR_CONTROL:
            if (descriptor_index >= controls_.size()) {
                return failure(make_error_code(NanoAvbError::InvalidDescriptorIndex));
            }
            return make_const_span(controls_[descriptor_index]);

        default:
            return failure(make_error_code(NanoAvbError::InvalidDescriptorType));
    }
}

// Private helpers

auto EntityModel::reserve_storage() -> void
{
    configurations_.reserve(config_.max_configurations);
    audio_units_.reserve(config_.max_audio_units);
    stream_inputs_.reserve(config_.max_stream_inputs);
    stream_outputs_.reserve(config_.max_stream_outputs);
    jack_inputs_.reserve(config_.max_jack_inputs);
    jack_outputs_.reserve(config_.max_jack_outputs);
    avb_interfaces_.reserve(config_.max_avb_interfaces);
    clock_sources_.reserve(config_.max_clock_sources);
    clock_domains_.reserve(config_.max_clock_domains);
    locales_.reserve(config_.max_locales);
    strings_.reserve(config_.max_strings);
    stream_port_inputs_.reserve(config_.max_stream_port_inputs);
    stream_port_outputs_.reserve(config_.max_stream_port_outputs);
    audio_clusters_.reserve(config_.max_audio_clusters);
    audio_maps_.reserve(config_.max_audio_maps);
    controls_.reserve(config_.max_controls);
}

}  // namespace statusbar::nanoavb
