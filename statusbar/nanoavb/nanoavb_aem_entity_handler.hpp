#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// NanoAVB AEM Entity Handler — abstract interface implemented by the
/// application to provide descriptor content on demand.
///
/// This is the Phase-3 replacement for the vector-backed `EntityModel`.
/// The new model does not own descriptor storage; instead, it delegates
/// every READ_DESCRIPTOR, GET_NAME, and SET_NAME request to a user-
/// supplied handler. Handlers receive a typed descriptor struct reference
/// (not raw bytes) and fill in the dynamic fields — they never do byte-
/// offset math into a wire buffer.
///
/// Symbol identifiers
/// ==================
/// Each on_get_* method receives a `uint32_t symbol` argument. Symbols
/// are pre-agreed application-layer identifiers that survive descriptor_index
/// reshuffling: if the AEM XML tooling is regenerated and the index of a
/// control changes, its symbol stays the same, so application code keeps
/// binding to the same abstract "volume control" entry. The AemEntityModel
/// looks up the symbol from the `DescriptorStorage` symbol table (when one
/// is attached) and passes it through to the handler; without a storage,
/// the symbol is 0 and the handler is expected to dispatch on
/// descriptor_index directly.
///
/// Default behaviour
/// =================
/// Every method has a no-op default implementation that returns "not
/// supported" — `false` for getters, `nullopt` for names, and
/// NOT_IMPLEMENTED for setters. Applications override only the methods
/// relevant to their entity.
///
/// Threading
/// =========
/// All methods are called from the reactor's single event-loop thread.
/// Handlers must not block or throw.

#include "statusbar/atdecc/atdecc_aecp_aem.hpp"
#include "statusbar/atdecc/atdecc_aem_descriptor.hpp"

#include <cstdint>
#include <optional>

namespace statusbar::nanoavb {

using atdecc::AEM_STATUS_NOT_IMPLEMENTED;
using atdecc::aem::AtdeccString;
using atdecc::aem::DescriptorAudioCluster;
using atdecc::aem::DescriptorAudioMap;
using atdecc::aem::DescriptorAudioUnit;
using atdecc::aem::DescriptorAvbInterface;
using atdecc::aem::DescriptorClockDomain;
using atdecc::aem::DescriptorClockSource;
using atdecc::aem::DescriptorConfiguration;
using atdecc::aem::DescriptorControl;
using atdecc::aem::DescriptorControlBlock;
using atdecc::aem::DescriptorEntity;
using atdecc::aem::DescriptorExternalPort;
using atdecc::aem::DescriptorInternalPort;
using atdecc::aem::DescriptorJack;
using atdecc::aem::DescriptorLocale;
using atdecc::aem::DescriptorMatrix;
using atdecc::aem::DescriptorMatrixSignal;
using atdecc::aem::DescriptorMemoryObject;
using atdecc::aem::DescriptorMixer;
using atdecc::aem::DescriptorPtpInstance;
using atdecc::aem::DescriptorPtpPort;
using atdecc::aem::DescriptorRef;
using atdecc::aem::DescriptorSensorCluster;
using atdecc::aem::DescriptorSensorMap;
using atdecc::aem::DescriptorSensorUnit;
using atdecc::aem::DescriptorSignalCombiner;
using atdecc::aem::DescriptorSignalDemultiplexer;
using atdecc::aem::DescriptorSignalMultiplexer;
using atdecc::aem::DescriptorSignalSelector;
using atdecc::aem::DescriptorSignalSplitter;
using atdecc::aem::DescriptorSignalTranscoder;
using atdecc::aem::DescriptorStream;
using atdecc::aem::DescriptorStreamPort;
using atdecc::aem::DescriptorStrings;
using atdecc::aem::DescriptorTiming;
using atdecc::aem::DescriptorVideoCluster;
using atdecc::aem::DescriptorVideoMap;
using atdecc::aem::DescriptorVideoUnit;
using atdecc::aem::NameRef;

/// Abstract base class for AEM descriptor providers.
///
/// Handlers implement only the descriptor types their entity uses.
/// Each on_get_* method receives a mutable reference to a default-
/// initialized (or DescriptorStorage-preloaded) descriptor struct;
/// the handler populates dynamic fields and returns true. Returning
/// false tells the AemEntityModel that the descriptor does not exist,
/// which maps to AEM_STATUS_NO_SUCH_DESCRIPTOR at the wire level.
///
/// One method per C++ struct type is provided. For pairs like
/// STREAM_INPUT/STREAM_OUTPUT that share a struct but carry different
/// descriptor_type codes, the handler can branch on
/// `ref.descriptor_type` inside a single `on_get_stream` override.
class AemEntityHandler
{
  public:
    AemEntityHandler() = default;
    virtual ~AemEntityHandler() = default;

    // Copy is disabled: concrete handlers often hold references or
    // application state that shouldn't be copied. Move is defaulted so
    // objects containing a handler-backed AemEntityModel (e.g. AemCommandHandler
    // inside NanoAvbComponents) can still be move-constructed. Derived
    // handler types that need to forbid movement should mark their own
    // members non-movable rather than deleting here.
    AemEntityHandler(AemEntityHandler const&) = delete;
    auto operator=(AemEntityHandler const&) -> AemEntityHandler& = delete;
    AemEntityHandler(AemEntityHandler&&) noexcept = default;
    auto operator=(AemEntityHandler&&) noexcept -> AemEntityHandler& = default;

    // ---- Top-level descriptors -------------------------------------------

    virtual auto on_get_entity(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorEntity& /*desc*/) -> bool { return false; }

    virtual auto on_get_configuration(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorConfiguration& /*desc*/) -> bool
    {
        return false;
    }

    // ---- Unit descriptors ------------------------------------------------

    virtual auto on_get_audio_unit(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorAudioUnit& /*desc*/) -> bool
    {
        return false;
    }

    virtual auto on_get_video_unit(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorVideoUnit& /*desc*/) -> bool
    {
        return false;
    }

    virtual auto on_get_sensor_unit(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorSensorUnit& /*desc*/) -> bool
    {
        return false;
    }

    // ---- Stream / Jack / Interface / Clock -------------------------------
    // Stream input and output share DescriptorStream. Branch on
    // ref.descriptor_type (DESCRIPTOR_STREAM_INPUT / DESCRIPTOR_STREAM_OUTPUT)
    // inside this method to distinguish direction.

    virtual auto on_get_stream(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorStream& /*desc*/) -> bool { return false; }

    /// Jack input and output share DescriptorJack. Branch on
    /// ref.descriptor_type (DESCRIPTOR_JACK_INPUT / DESCRIPTOR_JACK_OUTPUT).
    virtual auto on_get_jack(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorJack& /*desc*/) -> bool { return false; }

    virtual auto on_get_avb_interface(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorAvbInterface& /*desc*/) -> bool
    {
        return false;
    }

    virtual auto on_get_clock_source(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorClockSource& /*desc*/) -> bool
    {
        return false;
    }

    virtual auto on_get_clock_domain(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorClockDomain& /*desc*/) -> bool
    {
        return false;
    }

    // ---- Memory / Locale / Strings ---------------------------------------

    virtual auto on_get_memory_object(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorMemoryObject& /*desc*/) -> bool
    {
        return false;
    }

    virtual auto on_get_locale(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorLocale& /*desc*/) -> bool { return false; }

    virtual auto on_get_strings(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorStrings& /*desc*/) -> bool { return false; }

    // ---- Ports -----------------------------------------------------------
    // Each *_port descriptor has input and output variants sharing one
    // struct; branch on ref.descriptor_type inside the handler.

    virtual auto on_get_stream_port(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorStreamPort& /*desc*/) -> bool
    {
        return false;
    }

    virtual auto on_get_external_port(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorExternalPort& /*desc*/) -> bool
    {
        return false;
    }

    virtual auto on_get_internal_port(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorInternalPort& /*desc*/) -> bool
    {
        return false;
    }

    // ---- Clusters --------------------------------------------------------

    virtual auto on_get_audio_cluster(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorAudioCluster& /*desc*/) -> bool
    {
        return false;
    }

    virtual auto on_get_video_cluster(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorVideoCluster& /*desc*/) -> bool
    {
        return false;
    }

    virtual auto on_get_sensor_cluster(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorSensorCluster& /*desc*/) -> bool
    {
        return false;
    }

    // ---- Maps ------------------------------------------------------------

    virtual auto on_get_audio_map(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorAudioMap& /*desc*/) -> bool
    {
        return false;
    }

    virtual auto on_get_video_map(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorVideoMap& /*desc*/) -> bool
    {
        return false;
    }

    virtual auto on_get_sensor_map(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorSensorMap& /*desc*/) -> bool
    {
        return false;
    }

    // ---- Controls --------------------------------------------------------

    virtual auto on_get_control(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorControl& /*desc*/) -> bool { return false; }

    virtual auto on_get_control_block(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorControlBlock& /*desc*/) -> bool
    {
        return false;
    }

    // ---- Signal routing --------------------------------------------------

    virtual auto on_get_signal_selector(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorSignalSelector& /*desc*/) -> bool
    {
        return false;
    }

    virtual auto on_get_mixer(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorMixer& /*desc*/) -> bool { return false; }

    virtual auto on_get_matrix(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorMatrix& /*desc*/) -> bool { return false; }

    virtual auto on_get_matrix_signal(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorMatrixSignal& /*desc*/) -> bool
    {
        return false;
    }

    virtual auto on_get_signal_splitter(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorSignalSplitter& /*desc*/) -> bool
    {
        return false;
    }

    virtual auto on_get_signal_combiner(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorSignalCombiner& /*desc*/) -> bool
    {
        return false;
    }

    virtual auto on_get_signal_demultiplexer(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorSignalDemultiplexer& /*desc*/)
        -> bool
    {
        return false;
    }

    virtual auto on_get_signal_multiplexer(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorSignalMultiplexer& /*desc*/)
        -> bool
    {
        return false;
    }

    virtual auto on_get_signal_transcoder(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorSignalTranscoder& /*desc*/) -> bool
    {
        return false;
    }

    // ---- Timing / PTP ----------------------------------------------------

    virtual auto on_get_timing(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorTiming& /*desc*/) -> bool { return false; }

    virtual auto on_get_ptp_instance(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorPtpInstance& /*desc*/) -> bool
    {
        return false;
    }

    virtual auto on_get_ptp_port(DescriptorRef /*ref*/, uint32_t /*symbol*/, DescriptorPtpPort& /*desc*/) -> bool { return false; }

    // ---- Name get/set (cross-cutting, IEEE 1722.1 Clause 7.4.17/7.4.18) --

    /// Return the current value of the named field, or nullopt if the
    /// (descriptor, name_index) pair does not exist. Multiple names per
    /// descriptor are common — e.g. ENTITY has entity_name (index 0)
    /// and group_name (index 1).
    virtual auto on_get_name(NameRef /*ref*/, uint32_t /*symbol*/) const -> std::optional<AtdeccString> { return std::nullopt; }

    /// Apply a SET_NAME command. Return an AEM_STATUS_* code.
    /// AEM_STATUS_NOT_IMPLEMENTED is the appropriate default for read-only
    /// entities; return AEM_STATUS_SUCCESS after persisting the new name.
    virtual auto on_set_name(NameRef /*ref*/, uint32_t /*symbol*/, AtdeccString const& /*name*/) -> uint8_t
    {
        return AEM_STATUS_NOT_IMPLEMENTED;
    }
};

}  // namespace statusbar::nanoavb
