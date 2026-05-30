#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// DescriptorStorageHandler — a reusable AemEntityHandler backed by a
/// compiled-in (.aem blob) DescriptorStorage.
///
/// This is the canonical "serve everything from a blob" handler. It
/// overrides every on_get_* method to check whether the attached
/// DescriptorStorage has the requested (configuration, type, index)
/// triple and returns true if so. The actual descriptor bytes are
/// already populated into the struct by AemEntityModel's dispatch
/// wrapper (which calls span_load_padded from the same blob), so the
/// handler only needs to approve the response.
///
/// Intended use as a base class
/// ----------------------------
/// Applications that want to override a handful of fields at runtime
/// (e.g. inject the current entity_id, set the current_configuration,
/// patch a dynamic stream_format) should DERIVE from
/// DescriptorStorageHandler and override the handler methods they
/// care about. Each override should first call the base-class method
/// to let the storage preload happen, then patch the desired fields:
///
///     class MyHandler : public DescriptorStorageHandler {
///       public:
///         explicit MyHandler(DescriptorStorage s, Eui64 id)
///             : DescriptorStorageHandler{s}, entity_id_{id} {}
///
///         auto on_get_entity(DescriptorRef ref, uint32_t sym,
///                            DescriptorEntity& desc) -> bool override {
///             if (!DescriptorStorageHandler::on_get_entity(ref, sym, desc))
///                 return false;
///             desc.entity_id = entity_id_;
///             return true;
///         }
///     };
///
/// Standalone use
/// --------------
/// For read-only entities that just need to emit whatever the blob
/// contains (no runtime patching), an instance of DescriptorStorageHandler
/// itself can be passed directly to AemEntityModel / AemCommandHandler
/// without subclassing.

#include "statusbar/atdecc/atdecc_descriptor_storage.hpp"
#include "statusbar/buffer/span_utils.hpp"
#include "statusbar/nanoavb/nanoavb_aem_entity_handler.hpp"

namespace statusbar::nanoavb {

using atdecc::aem::DescriptorStorage;

/// Handler that serves descriptors out of an attached DescriptorStorage
/// blob. Each on_get_<type> override looks up the (configuration, type,
/// index) triple in the storage and, if found, loads the descriptor's
/// bytes into the caller-supplied struct via span_load_padded. Returns
/// false if the descriptor is not in the blob.
///
/// Self-preload rationale
/// ----------------------
/// AemEntityModel::dispatch_fixed can ALSO preload a descriptor from
/// a DescriptorStorage if one is attached to the model itself — but
/// callers often want to construct an AemEntityModel with just a
/// handler and no separate storage. The handler preloads from its own
/// storage so the behaviour is the same regardless of whether the
/// enclosing AemEntityModel was given a storage. Double-preloading
/// (when both the model and the handler point at the same storage) is
/// a no-op: the second memcpy overwrites the first with identical bytes.
///
/// Intended use as a base class
/// ----------------------------
/// Derive from DescriptorStorageHandler and override the handler
/// methods that need to patch dynamic fields after the storage
/// preload. Each override should first call the base-class method
/// (so the bytes are loaded and the storage presence check runs)
/// and then mutate the fields it owns:
///
///     class MyHandler : public DescriptorStorageHandler {
///       public:
///         explicit MyHandler(DescriptorStorage s, Eui64 id)
///             : DescriptorStorageHandler{s}, entity_id_{id} {}
///
///         auto on_get_entity(DescriptorRef ref, uint32_t sym,
///                            DescriptorEntity& desc) -> bool override {
///             if (!DescriptorStorageHandler::on_get_entity(ref, sym, desc))
///                 return false;
///             desc.entity_id = entity_id_;
///             return true;
///         }
///     };
class DescriptorStorageHandler : public AemEntityHandler
{
  public:
    explicit DescriptorStorageHandler(DescriptorStorage storage) noexcept
        : storage_{storage}
    {}

    // ---- Top-level ------------------------------------------------------

    auto on_get_entity(DescriptorRef ref, uint32_t /*symbol*/, DescriptorEntity& desc) -> bool override { return load(ref, desc); }

    auto on_get_configuration(DescriptorRef ref, uint32_t /*symbol*/, DescriptorConfiguration& desc) -> bool override
    {
        return load(ref, desc);
    }

    // ---- Unit descriptors -----------------------------------------------

    auto on_get_audio_unit(DescriptorRef ref, uint32_t /*symbol*/, DescriptorAudioUnit& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_video_unit(DescriptorRef ref, uint32_t /*symbol*/, DescriptorVideoUnit& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_sensor_unit(DescriptorRef ref, uint32_t /*symbol*/, DescriptorSensorUnit& desc) -> bool override
    {
        return load(ref, desc);
    }

    // ---- Streams / Jacks / Interface / Clock ----------------------------

    auto on_get_stream(DescriptorRef ref, uint32_t /*symbol*/, DescriptorStream& desc) -> bool override { return load(ref, desc); }

    auto on_get_jack(DescriptorRef ref, uint32_t /*symbol*/, DescriptorJack& desc) -> bool override { return load(ref, desc); }

    auto on_get_avb_interface(DescriptorRef ref, uint32_t /*symbol*/, DescriptorAvbInterface& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_clock_source(DescriptorRef ref, uint32_t /*symbol*/, DescriptorClockSource& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_clock_domain(DescriptorRef ref, uint32_t /*symbol*/, DescriptorClockDomain& desc) -> bool override
    {
        return load(ref, desc);
    }

    // ---- Memory / Locale / Strings --------------------------------------

    auto on_get_memory_object(DescriptorRef ref, uint32_t /*symbol*/, DescriptorMemoryObject& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_locale(DescriptorRef ref, uint32_t /*symbol*/, DescriptorLocale& desc) -> bool override { return load(ref, desc); }

    auto on_get_strings(DescriptorRef ref, uint32_t /*symbol*/, DescriptorStrings& desc) -> bool override
    {
        return load(ref, desc);
    }

    // ---- Ports ----------------------------------------------------------

    auto on_get_stream_port(DescriptorRef ref, uint32_t /*symbol*/, DescriptorStreamPort& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_external_port(DescriptorRef ref, uint32_t /*symbol*/, DescriptorExternalPort& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_internal_port(DescriptorRef ref, uint32_t /*symbol*/, DescriptorInternalPort& desc) -> bool override
    {
        return load(ref, desc);
    }

    // ---- Clusters / Maps / Controls -------------------------------------

    auto on_get_audio_cluster(DescriptorRef ref, uint32_t /*symbol*/, DescriptorAudioCluster& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_video_cluster(DescriptorRef ref, uint32_t /*symbol*/, DescriptorVideoCluster& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_sensor_cluster(DescriptorRef ref, uint32_t /*symbol*/, DescriptorSensorCluster& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_audio_map(DescriptorRef ref, uint32_t /*symbol*/, DescriptorAudioMap& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_video_map(DescriptorRef ref, uint32_t /*symbol*/, DescriptorVideoMap& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_sensor_map(DescriptorRef ref, uint32_t /*symbol*/, DescriptorSensorMap& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_control(DescriptorRef ref, uint32_t /*symbol*/, DescriptorControl& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_control_block(DescriptorRef ref, uint32_t /*symbol*/, DescriptorControlBlock& desc) -> bool override
    {
        return load(ref, desc);
    }

    // ---- Signal routing -------------------------------------------------

    auto on_get_signal_selector(DescriptorRef ref, uint32_t /*symbol*/, DescriptorSignalSelector& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_mixer(DescriptorRef ref, uint32_t /*symbol*/, DescriptorMixer& desc) -> bool override { return load(ref, desc); }

    auto on_get_matrix(DescriptorRef ref, uint32_t /*symbol*/, DescriptorMatrix& desc) -> bool override { return load(ref, desc); }

    auto on_get_matrix_signal(DescriptorRef ref, uint32_t /*symbol*/, DescriptorMatrixSignal& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_signal_splitter(DescriptorRef ref, uint32_t /*symbol*/, DescriptorSignalSplitter& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_signal_combiner(DescriptorRef ref, uint32_t /*symbol*/, DescriptorSignalCombiner& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_signal_demultiplexer(DescriptorRef ref, uint32_t /*symbol*/, DescriptorSignalDemultiplexer& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_signal_multiplexer(DescriptorRef ref, uint32_t /*symbol*/, DescriptorSignalMultiplexer& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_signal_transcoder(DescriptorRef ref, uint32_t /*symbol*/, DescriptorSignalTranscoder& desc) -> bool override
    {
        return load(ref, desc);
    }

    // ---- Timing / PTP ---------------------------------------------------

    auto on_get_timing(DescriptorRef ref, uint32_t /*symbol*/, DescriptorTiming& desc) -> bool override { return load(ref, desc); }

    auto on_get_ptp_instance(DescriptorRef ref, uint32_t /*symbol*/, DescriptorPtpInstance& desc) -> bool override
    {
        return load(ref, desc);
    }

    auto on_get_ptp_port(DescriptorRef ref, uint32_t /*symbol*/, DescriptorPtpPort& desc) -> bool override
    {
        return load(ref, desc);
    }

    /// Access the underlying storage (useful for derived handlers that
    /// want to look up additional blobs beyond descriptors).
    [[nodiscard]] auto storage() const noexcept -> DescriptorStorage const& { return storage_; }

  private:
    /// Load the descriptor bytes for `ref` into `desc` via
    /// span_load_padded. Returns false if the storage doesn't have a
    /// descriptor for `ref`. Used by every on_get_* override.
    template <typename T>
    auto load(DescriptorRef ref, T& desc) const noexcept -> bool
    {
        auto blob = storage_.get_descriptor(ref.configuration_index, ref.descriptor_type, ref.descriptor_index);
        if (!blob.has_value()) {
            return false;
        }
        span_load_padded(desc, *blob);
        return true;
    }

    DescriptorStorage storage_;
};

}  // namespace statusbar::nanoavb
