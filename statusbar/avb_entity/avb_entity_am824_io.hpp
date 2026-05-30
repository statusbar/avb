#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// AVB Entity AM824 I/O Module
/// Defines an AVB entity with one N-channel 48 kHz input stream (listener)
/// and one N-channel 48 kHz output stream (talker) using AM824 format.
/// The entity model is loaded from a descriptor storage blob.
/// Audio flows from listener through DSP biquad filters to talker.

#include "statusbar/atdecc/atdecc.hpp"
#include "statusbar/avtp/avtp.hpp"
#include "statusbar/dsp/dsp.hpp"
#include "statusbar/gptp/gptp.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/nanoavb/nanoavb.hpp"
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
#include <cstring>
#include <functional>
#include <memory>
#include <memory_resource>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statusbar::avb_entity {

using statusbar::failure;
using statusbar::Status;
using statusbar::StatusValue;
using statusbar::success;

/// Configuration for AVB AM824 I/O Entity
struct AvbEntityAm824IOConfig
{
    /// Entity ID (EUI-64) - unique identifier for this entity
    ieee::Eui64 entity_id{};

    /// Entity Model ID (EUI-64) - identifies the entity model/class
    ieee::Eui64 entity_model_id{};

    /// Descriptor storage blob (serialized AEM descriptors)
    std::vector<uint8_t> descriptor_storage_blob{};

    /// Network interface name (e.g., "en0" on macOS, "eth0" on Linux)
    std::string interface_name{"eth0"};

    /// Destination multicast MAC address for talker stream (no MAAP)
    ieee::Eui48 talker_dest_mac{0x71, 0xB3, 0xD5, 0xED, 0xCF, 0x00};

    /// VLAN ID for AVB streams (default: SR Class A VLAN 2)
    uint16_t vlan_id{2};

    /// DSP filter center frequency (Hz)
    double filter_freq_hz{1000.0};

    /// DSP filter gain (dB) - negative for cut, positive for boost
    double filter_gain_db{0.0};

    /// DSP filter Q factor
    double filter_q{0.707};

    /// Entity name (displayed in ATDECC controllers)
    std::string entity_name{"AVB AM824 IO"};

    /// Firmware version string
    std::string firmware_version{"1.0.0"};
};

/// Audio processing callback type
/// Called with interleaved N-channel samples
/// @param samples Input/output sample buffer (modified in place)
/// @param sample_count Number of sample frames (each frame contains N channel samples)
using Am824AudioProcessCallback = std::function<void(std::span<float> samples, size_t sample_count)>;

/// AVB Entity with N-channel AM824 input/output streams and DSP processing
///
/// This class implements an AVB entity that:
/// - Loads the entity model from a descriptor storage blob
/// - Receives audio via one N-channel 48 kHz AM824 listener stream
/// - Processes audio through configurable DSP biquad filters (one per channel)
/// - Transmits processed audio via one N-channel 48 kHz AM824 talker stream
///
/// The entity uses the nanoavb supervisor state machine to coordinate:
/// - gPTP synchronization
/// - MVRP VLAN registration
/// - MSRP stream reservation
/// - ACMP connection management
/// - ADP entity discovery
///
/// Example usage:
/// @code
///   AvbEntityAm824IOConfig config{
///       .entity_id = ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07},
///       .entity_model_id = ieee::Eui64{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77},
///       .descriptor_storage_blob = load_blob_from_file("entity.blob"),
///       .interface_name = "eth0",
///       .filter_gain_db = 0.0
///   };
///
///   auto result = AvbEntityAm824IO::create(std::move(config));
///   if (!result) { /* handle error */ }
///   auto entity = std::move(*result);
///
///   // Create reactor externally
///   net::MessageReactor reactor{shutdown_flag, net::monotonic_ns};
///
///   // Start the entity
///   entity.start(reactor);
///
///   // Run reactor loop
///   while (!shutdown) {
///       reactor.poll(100);
///   }
///
///   entity.stop();
/// @endcode
class AvbEntityAm824IO
{
  public:
    using TimePoint = sm::TimePoint;

    /// Stream format constants
    static constexpr uint32_t SAMPLE_RATE = 48000;
    static constexpr size_t SAMPLES_PER_PACKET = 6;

    /// Factory method — constructs and validates the entity from configuration
    /// Parses the descriptor storage blob to determine channel count and entity model
    /// @param config Entity configuration including network, stream, and DSP settings
    /// @param memory_resource Memory resource for the per-channel DSP
    ///        buffers (biquads_ and audio_buffer_). nullptr is treated
    ///        as std::pmr::get_default_resource().
    /// @return Heap-allocated entity on success, or error code on invalid blob
    [[nodiscard]] static auto create(AvbEntityAm824IOConfig config, std::pmr::memory_resource* memory_resource = nullptr)
        -> StatusValue<std::unique_ptr<AvbEntityAm824IO>>;

    /// Destructor - stops entity if running
    ~AvbEntityAm824IO();

    // Non-copyable, non-movable (state machine callbacks capture this)
    AvbEntityAm824IO(AvbEntityAm824IO const&) = delete;
    auto operator=(AvbEntityAm824IO const&) -> AvbEntityAm824IO& = delete;
    AvbEntityAm824IO(AvbEntityAm824IO&&) = delete;
    auto operator=(AvbEntityAm824IO&&) -> AvbEntityAm824IO& = delete;

    /// Start the entity and add handlers to reactor
    /// @param reactor Message reactor (caller must run poll loop)
    /// @return Status indicating success or failure
    [[nodiscard]] auto start(net::MessageReactor& reactor) -> Status;

    /// Stop the entity and clean up resources
    /// @return Status indicating success or failure
    [[nodiscard]] auto stop() -> Status;

    /// Check if entity is running
    [[nodiscard]] auto is_running() const noexcept -> bool { return running_; }

    /// Check if entity has reached Ready state (gPTP locked, VLAN registered)
    [[nodiscard]] auto is_ready() const noexcept -> bool;

    /// Get current supervisor state as string
    [[nodiscard]] auto state_string() const -> std::string;

    /// Print current state of all state machines
    auto print_state() const -> void;

    /// Handle link up event (call from external link monitor)
    /// @param time Current time point for state machine transition
    auto on_link_up(TimePoint time) -> void;

    /// Handle link down event (call from external link monitor)
    /// @param time Current time point for state machine transition
    auto on_link_down(TimePoint time) -> void;

    /// Handle gPTP announce received (called internally from gPTP handler)
    /// @param time Current time point for state machine transition
    /// @param has_grandmaster True if a gPTP grandmaster is present on the network
    auto on_gptp_announce(TimePoint time, bool has_grandmaster) -> void;

    /// Handle timeout in waiting states (call periodically)
    /// @param time Current time point for timeout evaluation
    auto on_timeout(TimePoint time) -> void;

    /// Process one audio packet period
    /// Called from PTP timer callback at packet rate (8000 Hz for 48 kHz audio)
    /// @param time Current PTP-synchronized time point for packet timestamping
    auto process_audio(TimePoint time) -> void;

    /// Set custom audio processing callback (in addition to biquad filter)
    /// @param callback Function called with interleaved N-channel samples for processing
    auto set_audio_callback(Am824AudioProcessCallback callback) -> void { audio_callback_ = std::move(callback); }

    /// Reconfigure DSP filter parameters (applies to all per-channel biquad filters)
    /// @param freq_hz Filter center frequency in Hz
    /// @param gain_db Filter gain in dB (negative for cut, positive for boost)
    /// @param q Filter Q factor (bandwidth)
    auto configure_filter(double freq_hz, double gain_db, double q) -> void;

    /// Get access to NanoAVB components (for advanced use)
    [[nodiscard]] auto components() -> nanoavb::NanoAvbComponents& { return components_; }
    [[nodiscard]] auto components() const -> nanoavb::NanoAvbComponents const& { return components_; }

    /// Get access to network handlers (for advanced use)
    [[nodiscard]] auto net_handlers() -> nanoavb::NanoAvbNetHandlers* { return net_handlers_.get(); }

    /// Get the configuration
    [[nodiscard]] auto config() const noexcept -> AvbEntityAm824IOConfig const& { return config_; }

    /// Get the number of audio channels
    [[nodiscard]] auto channels() const noexcept -> size_t { return channels_; }

  private:
    /// Private constructor — use create() to instantiate
    /// @param config  Validated configuration
    /// @param components Pre-built NanoAVB components
    /// @param channels   Number of audio channels derived from descriptor blob
    AvbEntityAm824IO(
        AvbEntityAm824IOConfig config,
        nanoavb::NanoAvbComponents components,
        size_t channels,
        std::pmr::memory_resource* memory_resource);

    /// Wire up state machine callbacks
    auto wire_callbacks() -> void;

    // Member order optimized to minimize struct padding
    // (config_ must precede components_ for initialization dependency)

    /// Configuration
    AvbEntityAm824IOConfig config_;

    /// Custom audio callback (optional)
    Am824AudioProcessCallback audio_callback_;

    //
    // State Machine Contexts (large structs grouped together)
    //

    nanoavb::gptp_sm::Context gptp_ctx_{};
    nanoavb::msrp_talker_sm::Context msrp_talker_ctx_{};
    nanoavb::msrp_listener_sm::Context msrp_listener_ctx_{};
    nanoavb::mvrp_sm::Context mvrp_ctx_{};
    nanoavb::supervisor_sm::Context supervisor_ctx_{};
    nanoavb::talker_engine_sm::Context talker_engine_ctx_{};
    nanoavb::listener_engine_sm::Context listener_engine_ctx_{};

    /// NanoAVB components (entity model, ADP, ACMP, MVRP, MSRP handlers)
    nanoavb::NanoAvbComponents components_;

    /// Network handlers (created on start)
    std::unique_ptr<nanoavb::NanoAvbNetHandlers> net_handlers_;

    /// Number of audio channels (determined from descriptor blob at construction)
    size_t channels_{0};

    //
    // DSP Processing
    //

    /// Memory resource for DSP buffers below.
    std::pmr::memory_resource* mem_resource_{std::pmr::get_default_resource()};

    /// Biquad filters (one per channel)
    std::pmr::vector<dsp::BiQuad<float>> biquads_;

    /// Audio processing buffer (interleaved, channels_ samples per frame)
    std::pmr::vector<float> audio_buffer_;

    /// Running state
    bool running_{false};

    //
    // State Machines (supervisor and protocol coordinators)
    //

    nanoavb::supervisor_sm::Machine supervisor_{};
    nanoavb::gptp_sm::Machine gptp_{};
    nanoavb::mvrp_sm::Machine mvrp_{};
    nanoavb::msrp_talker_sm::Machine msrp_talker_{};
    nanoavb::msrp_listener_sm::Machine msrp_listener_{};
    nanoavb::talker_engine_sm::Machine talker_engine_{};
    nanoavb::listener_engine_sm::Machine listener_engine_{};

    //
    // Packet Handling
    //

    /// Talker sequence number
    uint8_t talker_sequence_num_{0};

    /// Talker data block count
    uint8_t talker_dbc_{0};

    /// Talker stream ID (derived from entity ID)
    ieee::Eui64 talker_stream_id_{};
};

}  // namespace statusbar::avb_entity
