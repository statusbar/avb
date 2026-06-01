#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// AVB Entity Audio I/O Module (dual-format)
/// Defines an AVB entity with TWO talker stream sources and TWO listener stream
/// sinks, each N-channel 96 kHz:
///   - stream 0: AM824 (IEC 61883-6, MBLA 24-in-32)
///   - stream 1: AAF (IEEE 1722 AVTP Audio Format, 32-bit PCM)
/// Both streams share one audio engine (per-channel sine + biquad), one AVTP
/// transmit socket, and one stream RX port that dispatches by AVTP subtype.
/// The entity model is loaded from a descriptor storage blob that declares two
/// stream inputs and two stream outputs.

#include "statusbar/atdecc/atdecc.hpp"
#include "statusbar/avb_entity/avb_entity_aaf_reframe.hpp"
#include "statusbar/avb_entity/log_sweep_generator.hpp"
#include "statusbar/avb_entity/tx_pcap_recorder.hpp"
#include "statusbar/avtp/avtp.hpp"
#include "statusbar/avtp/avtp_aaf_stream_input.hpp"
#include "statusbar/avtp/avtp_aaf_stream_output.hpp"
#include "statusbar/avtp/avtp_am824_stream_input.hpp"
#include "statusbar/avtp/avtp_am824_stream_output.hpp"
#include "statusbar/avtp/avtp_crf_stream_output.hpp"
#include "statusbar/colbin/colbin_writer.hpp"
#include "statusbar/dsp/dsp.hpp"
#include "statusbar/gptp/gptp.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/nanoavb/nanoavb.hpp"
#include "statusbar/net/net_address.hpp"
#include "statusbar/net/net_message_reactor.hpp"
#include "statusbar/net/net_rawnet.hpp"
#include "statusbar/net/net_socket.hpp"
#include "statusbar/ptpclient/ptpclient.hpp"
#include "statusbar/ptpclient/ptpclient_freq_ratio.hpp"
#include "statusbar/ptpclient/ptpclient_media_clock.hpp"
#include "statusbar/ptpclient/ptpclient_tai_translator.hpp"
#include "statusbar/realtime/realtime.hpp"
#include "statusbar/sm/sm.hpp"
#include "statusbar/status/status.hpp"
#include "statusbar/stun/stun_rendezvous.hpp"
#include "statusbar/tsn/tsn.hpp"
#include "statusbar/udptun/udptun_aaf_v1_codec.hpp"
#include "statusbar/udptun/udptun_audio_egress.hpp"
#include "statusbar/udptun/udptun_audio_ingest.hpp"
#include "statusbar/udptun/udptun_csv_record.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace statusbar::avb_entity {

using statusbar::failure;
using statusbar::Status;
using statusbar::StatusValue;
using statusbar::success;

/// Configuration for the dual-format AVB Audio I/O Entity
struct AvbEntityAudioIOConfig
{
    /// Entity ID (EUI-64) - unique identifier for this entity
    ieee::Eui64 entity_id{};

    /// Entity Model ID (EUI-64) - identifies the entity model/class
    ieee::Eui64 entity_model_id{};

    /// Descriptor storage blob (serialized AEM descriptors; must declare 2
    /// stream inputs and 2 stream outputs)
    std::vector<uint8_t> descriptor_storage_blob{};

    /// Network interface name (e.g., "en0" on macOS, "eth0" on Linux)
    std::string interface_name{"eth0"};

    // Destination multicast MACs for the talker streams. NOTE: the IEEE-1722
    // AVTP multicast OUI 91:E0:F0 (00:00:00..00:FD:FF) is the MAAP dynamic
    // allocation pool and must NOT be hardcoded -- only used once claimed via
    // MAAP. Until MAAP is implemented we default to the JDKS OUI-36 (70:B3:D5:ED:C)
    // with the I/G (multicast) bit set (0x70->0x71), assuming one such device per
    // LAN; the top of that range (bottom 12 bits set) is reserved for the CRF.
    /// Destination multicast MAC for stream 0 (AM824 talker)
    ieee::Eui48 am824_talker_dest_mac{0x71, 0xB3, 0xD5, 0xED, 0xCF, 0xFD};

    /// Destination multicast MAC for stream 1 (AAF talker)
    ieee::Eui48 aaf_talker_dest_mac{0x71, 0xB3, 0xD5, 0xED, 0xCF, 0xFE};

    /// Destination multicast MAC for stream 2 (CRF media-clock talker) -- the
    /// highest address in the JDKS OUI-36 multicast range.
    ieee::Eui48 crf_talker_dest_mac{0x71, 0xB3, 0xD5, 0xED, 0xCF, 0xFF};

    /// CRF media-clock cadence (IEEE 1722-2016 §10). Matches the Milan CRF
    /// stream-input format (0x041060010000bb80): CRF_BASE_FREQUENCY=48 kHz base,
    /// timestamp_interval=96 events between timestamps, 1 timestamp per PDU -> 48000/96
    /// = 500 PDU/s, each timestamp spaced 96/48000 = 2 ms (= 192 of our 96 kHz audio
    /// samples). Receivers reject a mismatched interval/ts-per-PDU as UNSUPPORTED_FORMAT
    /// even when the base matches, so these must equal what the listener advertises.
    uint16_t crf_timestamp_interval{96};
    uint16_t crf_timestamps_per_packet{1};

    /// Gate talker stream transmission on listener readiness (IEEE 802.1Q SRP):
    /// a talker emits its AVTP stream only when a downstream listener has
    /// declared MSRP Listener Ready (listener_permits_transmit) for that stream
    /// OR has an active ACMP connection. With no listener / no Listener-Ready the
    /// talker stays silent instead of streaming into the void. The CRF media
    /// clock follows the audio talkers (emitted whenever any audio stream is
    /// transmitting). Default true; set false to stream unconditionally from
    /// link-up (the legacy behavior).
    bool gate_talker_on_listener{true};

    // Inter-site UDPTUN ingest (TX). When enabled and a peer is set, audio
    // received on the AAF listener (stream 1, interleaved int32) is reframed into
    // 1 ms packets and sent to the peer as AAF-v1 over IEEE-1722 Annex J UDP. Each
    // packet's avtp_timestamp is the sample's TAI presentation time, sourced as
    // CLOCK_REALTIME + udptun_tai_offset_ns (no PTP/PHC involvement); the far end
    // presents at tai + its worst-case-latency. The local AVB streams are
    // unchanged. See statusbar/udptun/udptun_audio_ingest.hpp.
    /// Enable the inter-site UDPTUN ingest TX path.
    bool udptun_enable{false};
    /// Far-site peer host/IP (empty disables the tunnel even if udptun_enable).
    std::string udptun_peer_host{};
    /// Far-site UDP port (IEEE 1722 Annex J continuous default 17220).
    uint16_t udptun_peer_port{17220};
    /// TAI − UTC offset (ns) applied to CLOCK_REALTIME to form the TAI timeline.
    int64_t udptun_tai_offset_ns{37'000'000'000};

    /// Which listener stream feeds the tunnel: 0 = AM824 (stream 0), 1 = AAF
    /// (stream 1, default). Both carry 4-byte samples (AM824 is 24-in-32), so the
    /// reframer handles either; the payload is forwarded opaquely.
    uint16_t udptun_source_stream{1};

    /// Frames (samples per channel) per inter-site packet. Default 44 = 458 us @
    /// 96 kHz, the largest size that keeps the datagram within one Ethernet MTU at
    /// 8 ch × int32 (32 B/frame): header (44 B) + 44×32 (1408 B) = 1452 B ≤ 1472
    /// (1500 − 20 IPv4 − 8 UDP), so NO IP fragmentation. 48 frames = a round 500 us
    /// but 1580 B → fragments into 2. Verified on the wire (jdk01b, 2026-06-08).
    uint16_t udptun_frames_per_packet{44};

    /// Inter-site UDPTUN egress (RX/playout). When enabled, the entity binds a UDP
    /// socket on udptun_listen_port, receives AAF-v1/AnnexJ packets from the far
    /// site, reclocks them onto the local media clock (play out at TAI + WCL with
    /// drop-to-0 concealment), and emits the de-tunneled audio on the local AVB
    /// talkers in place of the test oscillator. See AudioEgress.
    bool udptun_egress{false};
    /// UDP port the egress binds to receive far-site packets (Annex J default).
    uint16_t udptun_listen_port{17220};
    /// Worst-case-latency / playout delay (ns) for the egress de-jitter buffer.
    /// MUST be <= the AudioEgress ring depth (SlotCapacity*frames_per_packet/Fs ~=
    /// 29 ms) or the playout cursor reads behind where packets are retained and the
    /// egress permanently underruns. Default 20 ms: comfortably > real inter-site
    /// latency (6-9 ms measured) + jitter, and within the ring.
    int64_t udptun_wcl_ns{20'000'000};

    /// The ingest emits zero-PCM (silence) frames at the media-clock packet cadence
    /// whenever its listener source is absent or not delivering AVTP -- so the
    /// inter-site UDP transmission is NEVER gated on the AVB connection or incoming
    /// AVTP packets. Both ends therefore transmit continuously (silence or real
    /// audio), which keeps both NAT pinholes open and the tunnel solid + bidirectional
    /// regardless of source state; real audio simply replaces the silence the instant
    /// it arrives, with no tunnel re-establishment. DEFAULT TRUE: a tunnel node should
    /// not depend on a live source to hold its tunnel up. Set false only to suppress
    /// silence TX on a node that must stay quiet when idle.
    bool udptun_silence_source{true};

    /// Optional .colbin path: the egress records one row per received tunnel
    /// packet -- rx_TAI (CLOCK_REALTIME + tai_offset at drain), the packet's TAI
    /// presentation time, latency = rx_TAI - PT, and sequence -- in the owlm
    /// UdpTunCsvRecord schema, so scripts/owlm/owlm_analyze reads/plots it
    /// directly. Empty disables.
    std::string udptun_egress_colbin_path{};

    /// Pre-allocated capacity (bytes) for the egress colbin. The recorder maps
    /// this in full at start and NEVER grows -- no mid-run mremap to stall the
    /// real-time data plane (the growing mmap was the cause of the doubling
    /// latency spikes at t=base*2^k). Recording stops with capacity_exceeded if
    /// the run outlasts it. Default = 15 min @ 96 kHz (see
    /// udptun::default_colbin_capacity_bytes); raise via udptun.egress_colbin_max_mb.
    uint64_t udptun_egress_colbin_max_bytes{udptun::default_colbin_capacity_bytes};

    /// Send a temporally-shifted redundant copy of every ingest packet (owlm-
    /// style). The redundant carries the SAME sequence + TAI presentation time
    /// but a distinct stream_id (primary | UDPTUN_REDUN_BIT), sent
    /// udptun_temporal_shift_ms later. The far egress keys playout by TAI, so a
    /// redundant fills the slot of a lost primary (audio recovery); the colbin
    /// labels primary (RemotePrimary) vs redundant (RemoteRedundant) so
    /// owlm_analyze computes recovered / true_loss. Doubles the wire bandwidth.
    bool udptun_redundant{false};
    /// Delay (ms) between a primary and its redundant copy (default 5).
    int64_t udptun_temporal_shift_ms{5};

    /// STUN rendezvous for NAT traversal. When udptun_rendezvous_server is set
    /// (HOST:PORT), the entity performs a STUN REGISTER handshake at start() to
    /// discover the peer's reflexive address and obtain a hole-punched UDP socket,
    /// which becomes the SHARED socket for both ingest TX and egress RX (so the NAT
    /// mapping stays consistent). --udptun.peer/.listen are then ignored. Both
    /// peers must use the same session_id + key; roles are initiator/responder.
    std::string udptun_rendezvous_server{};           ///< STUN server HOST:PORT (empty = direct)
    std::string udptun_rendezvous_key{};              ///< 64-hex (32-byte) AES-128-SIV shared key
    std::string udptun_rendezvous_session_id{};       ///< 32-hex (16-byte) session id (same on both)
    std::string udptun_rendezvous_role{"initiator"};  ///< "initiator" or "responder"

    /// ATDECC wire version: "2016" emits descriptors at their IEEE 1722.1-2013/2016
    /// lengths (truncating the 2021-only descriptor tail fields) for controllers
    /// that reject 2021-length descriptors (e.g. Hive/Compass); "2021" emits the
    /// full 2021 forms. The ADPDU (68B) and the L2/short-form ACMPDU (56B) we use
    /// are identical across both standards; the 2021 IP-extended ACMPDU form
    /// (96B, control_data_length=84, carries the connected stream's IP address) is
    /// NOT implemented -- it is only needed to connect IP-transported streams, and
    /// is deferred until there is an IP-stream use case. Default "2016".
    std::string atdecc_version{"2016"};

    /// VLAN ID for AVB streams (default: SR Class A VLAN 2)
    uint16_t vlan_id{2};

    /// Sticky-Listener MSRP workaround. When true, the MSRP participant
    /// re-declares REGISTERED Listener attributes on every periodic/LeaveAll
    /// pass (not just our own declarations), echoing a downstream listener's
    /// Listener-Ready back toward the bridge so the bridge keeps the forwarding
    /// path to that listener warm. Needed for jdk01E -> the DSP processor via a Luminex
    /// switch, where cfea332 (strict end-station: never re-declare registered
    /// attributes) caused the switch to stop forwarding our stream to the DSP processor. Safe
    /// for the Listener type only (no two-source / TalkerFailed-19 conflict).
    /// Default OFF (strict, spec-compliant); enable per host that needs it.
    bool redeclare_registered_listeners{false};

    /// Suppress-LeaveAll MSRP workaround. When true, the MSRP participant never
    /// originates a periodic LeaveAll -- it only re-asserts its declarations via
    /// the periodic timer and never releases them. This reproduces the
    /// pre-006bf73 era that fed a the DSP processor through a Luminex switch reliably; the
    /// Luminex appears to drop installed stream forwarding when our LeaveAll
    /// drives the reservation through Leaving, making E->the DSP processor audio blink. We still
    /// honour received peer LeaveAlls; we just don't send our own. Default OFF
    /// (spec-compliant); enable per host talking to such a bridge.
    bool suppress_leaveall{false};

    /// TX stream-capture (diagnostic). When non-empty, the entity records its OWN
    /// transmitted stream frames (AAF / AM824 / CRF -- normally invisible because
    /// the TX socket is PACKET_QDISC_BYPASS) to this libpcap file, timestamped
    /// with the gPTP time at transmit, for `tx_pcap_seconds` from the first frame.
    /// Lets us inspect what we really put on the wire -- e.g. the AVTP presentation
    /// timestamps (the media clock handed to a listener) -- without a mirror port.
    /// Empty = disabled.
    std::string tx_pcap_path{};

    /// Capture window in seconds for tx_pcap_path (from the first transmitted
    /// frame). Default 10.
    uint32_t tx_pcap_seconds{10};

    /// Memory budget (bytes) for the TX capture ring. At 8000 pkt/s/stream and
    /// ~256 B/frame, 10 s of one stream is ~20 MB; default 64 MB covers three
    /// streams for 10 s. The window also stops if this fills.
    size_t tx_pcap_max_bytes{size_t{64} * 1024 * 1024};

    /// Test-signal generator: a repeating logarithmic sine sweep (chirp) on ONE
    /// channel of the UDP tunnel source. When enabled, the sweep REPLACES the
    /// tunnel's normal source (the AVB listener stream / silence keepalive) so the
    /// peer site receives the sweep -- e.g. jdk01B generates it, it crosses the
    /// tunnel to jdk01A's the audio interface channel `sweep_channel` out, and a physical patch
    /// 8A ch1 -> ch2 returns it through the tunnel for a round-trip latency/
    /// continuity test. Does NOT affect the local talker (the local the audio interface still gets
    /// the tone/de-tunneled audio). Disabled by default.
    bool sweep_enable{false};
    double sweep_f_start_hz{20.0};
    double sweep_f_end_hz{1000.0};
    double sweep_duration_s{5.0};
    uint16_t sweep_channel{0};  // first channel; others carry silence
    float sweep_amplitude{0.5F};

    /// 802.1Q priority code point (PCP) for AVB stream frames. AVTP stream frames
    /// must be VLAN-tagged with the SR class priority so bridges classify them
    /// into the right SR class / credit-based shaper. Default 3 = SR Class A.
    uint8_t stream_pcp{3};

    /// Presentation-time offset in nanoseconds added to the gPTP time to form the
    /// AVTP timestamp of transmitted stream packets (how far in the future the
    /// listener should present the samples). Must exceed the worst-case talker +
    /// network transit time or the listener flags LATE_TIMESTAMP. Default 1 ms
    /// (the deterministic GPS-rate media clock owns this lead; see
    /// ptpclient::MediaClockGenerator).
    uint64_t presentation_offset_ns{1'000'000};

    /// Number of stream packets emitted per stream per media-timer wake. Default 1
    /// = strict SR Class A (one 12-sample/125 us packet per 125 us wake, evenly
    /// shaped). Setting N>1 wakes every N*125 us and emits N back-to-back packets
    /// per stream per wake (each still a 12-sample/125 us Class A frame, avtp
    /// timestamps advanced one packet interval apart), so the on-wire packet rate
    /// (8000/s/stream) and frame format are UNCHANGED, but there are N x fewer
    /// timer deadlines to miss. The burst-of-N egress is NOT strict Class A
    /// traffic shaping; it is absorbed at the listener by presentation_offset_ns,
    /// which must exceed N*125 us (it does at the 2 ms default). Non-default.
    size_t packets_per_wake{1};

    /// Pin the media-clock rate ratio r to exactly 1.0 (media clock = gPTP) instead
    /// of tracking r = gPTP/CLOCK_REALTIME via the GPS ratio Kalman. Use when the
    /// gPTP grandmaster IS the reference the listener recovers against (e.g. the
    /// listener is itself the GM, or the GM and listener share one timebase) and
    /// CLOCK_REALTIME is NOT GPS-disciplined (NTP), so r would otherwise wander and
    /// wobble the recovered clock. Skips the CLOCK_REALTIME sampling entirely.
    /// Default false (GPS-rate-pinned, for multi-site agreement). Diagnostic/test.
    bool media_lock_to_gptp{false};

    /// MEDIA_LOCKED detector tolerance (ns). A received stream locks when 8
    /// consecutive avtp_timestamp deltas fall within +/- this of the expected
    /// samples_per_ch/SAMPLE_RATE interval (125 us @ 12 samples). Default 5000
    /// (5 us). Widen to tolerate talker media-timer jitter without flapping the
    /// lock (e.g. when the talker batches packets per wake or runs hot).
    uint32_t lock_tolerance_ns{5'000};

    /// DSP filter center frequency (Hz)
    double filter_freq_hz{1000.0};

    /// DSP filter gain (dB) - negative for cut, positive for boost
    double filter_gain_db{0.0};

    /// DSP filter Q factor
    double filter_q{0.707};

    /// Stream test-tone frequency (Hz) generated per channel. Default 96000/7.
    double tone_freq_hz{96000.0 / 7.0};

    /// Stream test-tone amplitude (0..1 linear).
    float tone_amplitude{0.5F};

    /// Entity name (displayed in ATDECC controllers)
    std::string entity_name{"AVB Audio IO"};

    /// Firmware version string
    std::string firmware_version{"1.0.0"};
};

/// Audio processing callback type. Called with interleaved N-channel samples.
using AudioProcessCallback = std::function<void(std::span<float> samples, size_t sample_count)>;

/// AVB Entity with dual-format (AM824 + AAF) N-channel audio streams.
///
/// Talker stream 0 transmits AM824 to am824_talker_dest_mac; talker stream 1
/// transmits AAF (int32 PCM) to aaf_talker_dest_mac. The single stream RX port
/// joins both multicast groups and dispatches received frames by AVTP subtype
/// (0x00 -> AM824, 0x02 -> AAF). Both talkers carry the same per-channel sine
/// source (distinct phase per channel) so the two formats are directly
/// comparable on the wire. Coordination (gPTP, MVRP, MSRP, ACMP, ADP) reuses the
/// nanoavb supervisor stack exactly as the single-format AM824 entity does.
class AvbEntityAudioIO
{
  public:
    using TimePoint = sm::TimePoint;

    /// Stream format constants. 96 kHz, SR class A (125 us interval = 8000
    /// packets/s), so samples-per-packet = 96000/8000 = 12.
    static constexpr uint32_t CLASS_A_PACKETS_PER_SEC = 8000;
    static constexpr uint32_t SAMPLE_RATE = 96000;
    static constexpr size_t SAMPLES_PER_PACKET = SAMPLE_RATE / CLASS_A_PACKETS_PER_SEC;

    /// CRF (media-clock reference) base frequency. Milan mandates a 48 kHz CRF
    /// media clock; 48 kHz AND 96 kHz clients both lock to it. Our audio runs at
    /// SAMPLE_RATE (96 kHz), so each CRF timestamp covers SAMPLE_RATE/CRF_BASE_FREQUENCY
    /// (= 2) audio samples -- the on-wire timestamp values stay spaced at
    /// 1/CRF_BASE_FREQUENCY s. Must divide SAMPLE_RATE evenly.
    static constexpr uint32_t CRF_BASE_FREQUENCY = 48000;
    static_assert(SAMPLE_RATE % CRF_BASE_FREQUENCY == 0, "CRF base must divide the audio sample rate");

    /// Redundancy flag bit in the tunnel stream_id: the redundant copy is sent with
    /// `primary | UDPTUN_REDUN_BIT`, so the egress tells primary from redundant. It
    /// MUST sit in the modified-EUI-64 middle bytes (b3/b4 = the inserted 0xFF:0xFE,
    /// uint64 bits 24..39) — the bytes owlm_analyze masks when grouping primary +
    /// redundant into one logical sender — otherwise the two copies land in different
    /// pair_ids and recovery accounting breaks. Bit 24 (LSB of b4 = 0xFE) is reliably
    /// clear in a MAC-derived id and inside owlm's mask. (A NIC-half bit like 1<<23
    /// was wrong on both counts: it can be set in the MAC, and is outside owlm's
    /// mask.) The primary id force-clears this bit so the redundant id is always
    /// distinct. The b3/b4 mask owlm applies for pairing is OWLM_PAIR_MASK_MIDBYTES.
    static constexpr uint64_t UDPTUN_REDUN_BIT = (1ULL << 24);
    static constexpr uint64_t OWLM_PAIR_MASK_MIDBYTES = 0x000000FF'FF000000ULL;
    static_assert((UDPTUN_REDUN_BIT & (UDPTUN_REDUN_BIT - 1)) == 0, "REDUN_BIT must be a single bit");
    static_assert(
        (UDPTUN_REDUN_BIT & ~OWLM_PAIR_MASK_MIDBYTES) == 0,
        "REDUN_BIT must live in the EUI-64 b3/b4 bytes that owlm masks for pair grouping");

    /// AAF stream wire format: 32-bit signed PCM at 96 kHz.
    static constexpr avtp::AafFormat AAF_FORMAT = avtp::AafFormat::int_32bit;
    static constexpr avtp::AafSampleRate AAF_SAMPLE_RATE = avtp::AafSampleRate::rate_96_khz;
    static constexpr uint8_t AAF_BIT_DEPTH = 32;

    /// Stream descriptor indices.
    static constexpr uint16_t AM824_STREAM_INDEX = 0;
    static constexpr uint16_t AAF_STREAM_INDEX = 1;
    static constexpr uint16_t CRF_STREAM_INDEX = 2;  // media-clock (no audio, no listener sink)

    /// Factory method — constructs and validates the entity from configuration.
    /// Parses the descriptor storage blob (which must declare >=2 stream inputs
    /// and >=2 stream outputs) to determine channel count and entity model.
    [[nodiscard]] static auto create(AvbEntityAudioIOConfig config, std::pmr::memory_resource* memory_resource = nullptr)
        -> StatusValue<std::unique_ptr<AvbEntityAudioIO>>;

    ~AvbEntityAudioIO();

    AvbEntityAudioIO(AvbEntityAudioIO const&) = delete;
    auto operator=(AvbEntityAudioIO const&) -> AvbEntityAudioIO& = delete;
    AvbEntityAudioIO(AvbEntityAudioIO&&) = delete;
    auto operator=(AvbEntityAudioIO&&) -> AvbEntityAudioIO& = delete;

    /// Passkey gating the public constructor (see AvbEntityAm824IO::CreateKey).
    class CreateKey
    {
        CreateKey() = default;
        friend class AvbEntityAudioIO;
    };

    AvbEntityAudioIO(
        CreateKey,
        AvbEntityAudioIOConfig config,
        nanoavb::EntityModel entity_model,
        size_t channels,
        std::pmr::memory_resource* memory_resource);

    /// Start the entity and add handlers to reactor
    [[nodiscard]] auto start(net::MessageReactor& reactor) -> Status;

    /// Stop the entity and clean up resources
    [[nodiscard]] auto stop() -> Status;

    [[nodiscard]] auto is_running() const noexcept -> bool { return running_; }
    [[nodiscard]] auto is_ready() const noexcept -> bool;
    [[nodiscard]] auto state_string() const -> std::string_view;
    auto print_state() const -> void;

    auto on_link_up(TimePoint time) -> void;
    auto on_link_down(TimePoint time) -> void;
    auto on_gptp_announce(TimePoint time, bool has_grandmaster) -> void;
    auto on_timeout(TimePoint time) -> void;

    /// Process one audio packet period (called from PTP timer at 8000 Hz):
    /// generate audio once, then transmit both an AM824 and an AAF packet.
    auto process_audio(TimePoint time) -> void;

    /// Whether talker stream `idx` (0=AM824, 1=AAF, 2=CRF) should put its AVTP
    /// stream on the wire this tick: true if gating is disabled, or a downstream
    /// listener is ready (MSRP Listener Ready) / connected (ACMP) for the stream.
    [[nodiscard]] auto talker_should_transmit(uint16_t idx, int64_t now_ns) const noexcept -> bool;

    /// Decode one received AVTP stream frame, dispatching by subtype to the
    /// AM824 or AAF listener. Called from the stream RX handler.
    void on_stream_rx_frame(std::span<uint8_t const> frame, int64_t now_ns);

    auto set_audio_callback(AudioProcessCallback callback) -> void { audio_callback_ = std::move(callback); }
    auto configure_filter(double freq_hz, double gain_db, double q) -> void;

    [[nodiscard]] auto components() -> nanoavb::NanoAvbComponents& { return components_; }
    [[nodiscard]] auto components() const -> nanoavb::NanoAvbComponents const& { return components_; }
    [[nodiscard]] auto net_handlers() -> nanoavb::NanoAvbNetHandlers* { return net_handlers_.get(); }
    [[nodiscard]] auto config() const noexcept -> AvbEntityAudioIOConfig const& { return config_; }
    [[nodiscard]] auto channels() const noexcept -> size_t { return channels_; }

    /// TX stream-capture (see AvbEntityAudioIOConfig::tx_pcap_path). The capture
    /// itself runs RT-safe on the media thread; the FILE write must happen off the
    /// RT path, so the non-RT main loop polls tx_pcap_ready_to_write() after each
    /// poll and calls flush_tx_pcap() once to persist the file.
    [[nodiscard]] auto tx_pcap_ready_to_write() const noexcept -> bool { return tx_pcap_recorder_.ready_to_write(); }
    [[nodiscard]] auto flush_tx_pcap() -> Status { return tx_pcap_recorder_.write_to_file(); }
    [[nodiscard]] auto tx_pcap_frame_count() const noexcept -> size_t { return tx_pcap_recorder_.frame_count(); }

  private:
    auto wire_callbacks() -> void;

    /// Build the MSRP talker reservation for the AM824 stream (stream 0). MSRP
    /// advertises a single talker reservation matching the existing single-format
    /// entity; both data-plane streams transmit regardless (link-up driven).
    [[nodiscard]] auto make_talker_srp_info(uint16_t stream_index) const -> nanoavb::TalkerStreamSrpInfo;

    /// Transmit one AM824 packet (stream 0) of `samples` data blocks with the
    /// given deterministic presentation timestamp base. Reads audio_buffer_.
    /// AM824 (IEC 61883-6) carries SYT/DBC, so a variable per-packet blocking
    /// factor (the GPS-paced `samples`) is legal.
    void transmit_am824(uint64_t now_ns, uint32_t samples);

    /// Transmit one AAF packet (stream 1) of EXACTLY `samples` frames read from
    /// `src` (interleaved float, `samples * channels_`). AAF has no SYT/DBC, so
    /// `samples_per_frame` is fixed by the advertised stream format; a Milan
    /// listener rejects any packet whose payload doesn't decode to that count.
    /// Callers therefore pass a constant SAMPLES_PER_PACKET via the reframer.
    void transmit_aaf(uint64_t now_ns, uint16_t samples, std::span<float const> src);

    /// Transmit one CRF (media-clock) packet: timestamps_per_packet event times
    /// pulled from the GPS-locked media clock, evenly spaced by timestamp_interval.
    void transmit_crf();

    /// Periodically (re)estimate the GPS frequency ratio r = switch/GPS by
    /// sampling CLOCK_REALTIME (GPS, via chrony) against the gPTP media time, and
    /// feed it to the deterministic media-clock generator. Runs on the media
    /// thread; cheap and rate-limited.
    void update_gps_ratio(uint64_t gptp_now_ns);

    AvbEntityAudioIOConfig config_;
    AudioProcessCallback audio_callback_;

    nanoavb::gptp_sm::Context gptp_ctx_{};
    nanoavb::msrp_talker_sm::Context msrp_talker_ctx_{};
    nanoavb::msrp_listener_sm::Context msrp_listener_ctx_{};
    nanoavb::mvrp_sm::Context mvrp_ctx_{};
    nanoavb::supervisor_sm::Context supervisor_ctx_{};
    nanoavb::talker_engine_sm::Context talker_engine_ctx_{};
    nanoavb::listener_engine_sm::Context listener_engine_ctx_{};

    nanoavb::NanoAvbComponents components_;
    std::unique_ptr<nanoavb::NanoAvbNetHandlers> net_handlers_;

    size_t channels_{0};

    //
    // DSP processing (shared by both talker streams)
    //
    std::pmr::memory_resource* mem_resource_{std::pmr::get_default_resource()};
    std::pmr::vector<dsp::BiQuad<float>> biquads_;
    std::pmr::vector<float> audio_buffer_;
    std::pmr::vector<dsp::Oscillator<float>> oscillators_;

    //
    // Stream data plane (one TX socket shared by both talkers)
    //
    net::RawnetContext stream_tx_{};

    /// Stream 0: AM824 talker/listener contexts + destination MAC.
    std::optional<avtp::Am824StreamOutputContext> am824_out_{};
    std::optional<avtp::Am824StreamInputContext> am824_in_{};
    ieee::Eui48 am824_dest_mac_{};

    /// Stream 1: AAF talker/listener contexts + destination MAC.
    std::optional<avtp::AafStreamOutputContext> aaf_out_{};
    std::optional<avtp::AafStreamInputContext> aaf_in_{};
    ieee::Eui48 aaf_dest_mac_{};

    /// AAF reframe FIFO: the media timer wakes on gPTP but the media clock is
    /// GPS/TAI-paced, so each wake yields a VARIABLE sample count. AM824 sends
    /// that directly; AAF must emit a CONSTANT SAMPLES_PER_PACKET, so the
    /// reframer buffers the GPS-paced samples and drains them in fixed blocks
    /// (carrying the <block remainder to the next wake).
    AafReframer aaf_reframer_;

    /// Stream 2: CRF media-clock talker context + destination MAC. Driven by the
    /// same GPS-locked media clock as the audio; carries no audio, no listener.
    std::optional<avtp::CrfStreamOutputContext> crf_out_{};
    ieee::Eui48 crf_dest_mac_{};
    uint64_t crf_event_{0};  ///< media-clock event index for the next CRF timestamp
    uint16_t crf_decim_{0};  ///< audio-packet counter to decimate CRF emission to its PDU rate

    /// Non-owning handle to the StreamRxHandler's RX socket (owned by the reactor).
    /// Set in start() before the handler is moved into the reactor; used to join a
    /// remote talker's stream multicast group when our listener connects (and leave
    /// on disconnect), so listener reception needs no manual `ip maddr add`.
    net::RawnetContext* rx_sock_{nullptr};

    /// Per-talker-stream "a listener permits transmit" (MSRP Listener Ready),
    /// indexed by stream (0=AM824, 1=AAF, 2=CRF). Written by the on_talker_listener
    /// callback on the reactor thread, read by the media-timer thread in the
    /// transmit gate -> atomic. See talker_should_transmit() / gate_talker_on_listener.
    std::array<std::atomic<bool>, 3> msrp_listener_ready_{};

    /// Last steady-clock time (ns) each talker stream's MSRP Listener Ready was
    /// observed true. The strict ACMP-AND-MSRP gate keeps the stream transmitting
    /// for a grace window past this, so a normal MRP LeaveAll re-registration blip
    /// (Listener Ready momentarily withdrawn then re-declared) does NOT chop the
    /// stream -- the reason the strict gate was previously reverted to ACMP-only.
    std::array<std::atomic<int64_t>, 3> msrp_ready_ns_{};

    /// Per-format data-plane counters. tx touched only on the PTP thread; rx
    /// counters are atomic (written on the reactor thread, read for status).
    uint64_t am824_tx_packets_{0};
    std::atomic<uint64_t> am824_rx_packets_{0};
    std::atomic<uint64_t> am824_rx_samples_{0};
    std::atomic<uint64_t> am824_rx_bad_{0};

    uint64_t aaf_tx_packets_{0};

    // TX stream capture (diagnostic; see config tx_pcap_path). last_tx_gptp_ns_ is
    // set just before each stream send so the socket egress tap can timestamp the
    // captured frame with the gPTP transmit time.
    TxPcapRecorder tx_pcap_recorder_{};
    uint64_t last_tx_gptp_ns_{0};
    std::atomic<uint64_t> aaf_rx_packets_{0};
    std::atomic<uint64_t> aaf_rx_samples_{0};
    std::atomic<uint64_t> aaf_rx_bad_{0};

    /// IEEE 1722.1 STREAM_INPUT counters (Clause 7.4.42) for incoming-stream
    /// health, queryable via AECP GET_COUNTERS. Published values are atomic
    /// (written on the RX thread, read by the AECP handler); the lock/sequence
    /// detector state below is touched only on the RX thread.
    struct StreamInputCounters
    {
        std::atomic<uint32_t> media_locked{0};
        std::atomic<uint32_t> media_unlocked{0};
        std::atomic<uint32_t> seq_num_mismatch{0};
        std::atomic<uint32_t> media_reset{0};
        std::atomic<uint32_t> timestamp_uncertain{0};
        std::atomic<uint32_t> timestamp_valid{0};
        std::atomic<uint32_t> timestamp_not_valid{0};
        std::atomic<uint32_t> unsupported_format{0};
        std::atomic<uint32_t> late_timestamp{0};
        std::atomic<uint32_t> early_timestamp{0};
        std::atomic<uint32_t> frames_rx{0};

        bool have_prev{false};
        bool have_prev_ts{false};
        uint8_t prev_seq{0};
        bool prev_mr{false};
        uint32_t prev_ts{0};
        // Previous inter-(valid-)timestamp step, for constant-step media lock. AAF
        // steps 125 us/packet; AM824's avtp_timestamp follows the 61883-6 SYT
        // cadence (SYT_INTERVAL=16 @ 96 kHz -> 166.67 us between valid stamps, on 3
        // of every 4 packets), so we lock on a STEADY step, not a hardcoded value.
        uint32_t prev_delta{0};
        bool have_prev_delta{false};
        int locked_run{0};
        bool is_locked{false};
    };
    std::array<StreamInputCounters, 2> stream_in_counters_{};  // [0]=AM824, [1]=AAF

    /// Latest gPTP time (ns) seen by the media timer; the RX thread reads it as
    /// "gPTP now" (<=125 us stale) for LATE/EARLY_TIMESTAMP detection, since the
    /// reactor's own clock is CLOCK_MONOTONIC, not gPTP.
    std::atomic<uint64_t> last_gptp_ns_{0};

    //
    // Inter-site UDPTUN ingest (TX): AAF listener (int32) -> 1 ms TAI-stamped
    // AAF-v1/AnnexJ packets -> UDP peer. All touched only on the RX/reactor
    // thread (where on_stream_rx_frame runs); the inline non-blocking sendto of
    // a ~3 KB datagram is cheap. Active only when config_.udptun_enable and a
    // peer host is configured.
    //
    bool udptun_enable_{false};
    net::FileDescriptor udptun_fd_{};
    net::SocketAddress udptun_peer_{};
    std::optional<udptun::AudioIngest<>> udptun_ingest_{};
    std::optional<udptun::AafV1OverAnnexJCodec> udptun_codec_{};
    std::vector<uint8_t> udptun_txbuf_{};
    ieee::Eui64 udptun_stream_id_{};
    uint32_t udptun_seq_{0};
    bool udptun_anchored_{false};
    std::atomic<uint64_t> udptun_tx_packets_{0};
    ieee::Eui64 udptun_redundant_id_{};
    /// Circular delay line of recent ingest packets, replayed `depth` sends
    /// later as the redundant copy. Each slot's pcm is pre-sized at setup to
    /// avoid hot-path allocation.
    struct UdptunRedunSlot
    {
        uint32_t seq{0};
        int64_t tai{0};
        bool valid{false};
        std::vector<uint8_t> pcm{};
    };
    std::vector<UdptunRedunSlot> udptun_redun_ring_{};
    size_t udptun_redun_head_{0};
    size_t udptun_redun_depth_{0};
    /// True when udptun_fd_ is a STUN-traversed socket shared by TX and RX.
    bool udptun_rendezvous_active_{false};
    /// True when udptun_fd_ is a single socket shared by ingest TX and egress RX
    /// -- the rendezvous path OR the direct-peer bidirectional path (both ends
    /// bind udptun_listen_port and transmit, opening both NAT pinholes without
    /// STUN). When false, ingest TX and egress RX use separate sockets.
    bool udptun_shared_socket_{false};
    // True for the DIRECT-SHARED tunnel (no STUN). Stable for the life of the
    // socket (unlike udptun_shared_socket_, which a teardown clears). Gates the
    // media-thread keepalive ON and the STUN-only re-punch/teardown paths OFF.
    bool udptun_direct_shared_mode_{false};
    /// Pre-zeroed silence block for udptun_silence_source (never written; sliced
    /// to the per-tick frame count and fed to the ingest as zero PCM).
    std::vector<uint8_t> udptun_silence_buf_{};

    /// Test-signal sweep generator (config sweep_*) + its interleaved int32 scratch
    /// block. When sweep_enable, udptun_ingest_sweep() fills the chirp on
    /// sweep_channel (silence elsewhere) and feeds it as the tunnel source.
    LogSweepGenerator sweep_gen_{};
    std::vector<uint8_t> sweep_buf_{};
    // TAI pacing for the sweep tunnel source: the tunnel clock is the GPS-TAI
    // timeline (tai_translator_), so emit exactly SAMPLE_RATE frames per second of
    // that TAI. Cumulative-target pacing keeps the ingest avtp_timestamp locked to
    // TAI with zero drift. Anchor (=0 => unset) resets when the ingest re-anchors.
    int64_t sweep_tai_anchor_ns_{0};
    uint64_t sweep_frames_emitted_{0};

    /// Scratch for udptun_ingest_am824_as_int32: AM824 MBLA quadlets transcoded to
    /// int32 PCM before ingest (pre-sized to one media tick; grows if exceeded).
    std::vector<uint8_t> udptun_am824_transcode_buf_{};

    // Inter-site UDPTUN egress (RX/playout): far-site packets -> AudioEgress ->
    // local AVB talkers. The UDP socket is drained on the media-timer thread so
    // AudioEgress stays single-threaded (no cross-thread queue).
    bool udptun_egress_active_{false};
    net::FileDescriptor udptun_rx_fd_{};
    std::optional<udptun::AudioEgress<>> udptun_egress_{};
    std::optional<udptun::AafV1OverAnnexJCodec> udptun_egress_codec_{};
    std::vector<uint8_t> udptun_rxbuf_{};       ///< one-datagram recv scratch
    std::vector<uint8_t> udptun_egress_pcm_{};  ///< per-tick playout scratch (int32)
    std::atomic<uint64_t> udptun_rx_packets_{0};
    /// Per-packet egress timing recorder (.colbin, owlm UdpTunCsvRecord schema).
    /// Written on the media-timer thread at drain (mmap append, no syscall).
    std::optional<statusbar::colbin::Writer> udptun_egress_colbin_{};

    // --- Async STUN-rendezvous punch-RETRY (see start_udptun_punch_worker) ---
    // The worker thread does the blocking perform_rendezvous off the hot path and
    // STAGES the hole-punched socket; the media thread (process_audio) INSTALLS it
    // into udptun_fd_ and watchdogs the RX. Only the media thread touches
    // udptun_fd_ during operation -- the worker only writes the staging slot.
    std::thread udptun_punch_thread_{};
    std::atomic<bool> udptun_punch_run_{false};    ///< worker keeps looping while true
    std::mutex udptun_stage_mutex_{};              ///< guards the staging slot below
    net::FileDescriptor udptun_staged_fd_{};       ///< guarded by udptun_stage_mutex_
    net::SocketAddress udptun_staged_peer_{};      ///< guarded by udptun_stage_mutex_
    bool udptun_staged_ready_{false};              ///< guarded by udptun_stage_mutex_
    std::atomic<bool> udptun_punch_retry_{false};  ///< media -> worker: re-punch now
    int64_t udptun_install_tai_ns_{0};             ///< media: when current socket installed (0 = none)
    uint64_t udptun_rx_baseline_{0};               ///< media: any-rx snapshot for liveness detection
    int64_t udptun_last_rx_ns_{0};                 ///< media: last time any-rx advanced
    bool udptun_saw_data_{false};                  ///< media: any datagram seen since this install
    // Egress-anchor self-heal (media thread only): if the tunnel is delivering data
    // but the egress emits no REAL audio for a while, its anchor/timeline is stale
    // (e.g. a punch->egress startup race) -- reset() it so it re-anchors on the next
    // packet. Also reset on every socket (re)install so a fresh tunnel starts clean.
    uint64_t udptun_egress_real_frames_{0};        ///< media: cumulative real (non-concealed) egress frames
    uint64_t udptun_egress_play_baseline_{0};      ///< media: watchdog snapshot of the above
    int64_t udptun_egress_last_play_ns_{0};        ///< media: last time real egress frames advanced
    uint64_t udptun_egress_audio_rx_baseline_{0};  ///< media: watchdog snapshot of udptun_rx_packets_ (decoded audio)
    int64_t udptun_egress_last_audio_ns_{0};       ///< media: last time decoded tunnel audio arrived
    int udptun_egress_reset_streak_{0};            ///< media: consecutive anchor resets without recovery (escalates to re-punch)
    // RT-safe event counters: the media thread (udptun_punch_service) bumps these
    // instead of std::print-ing (stderr I/O can block/alloc on the RT path);
    // print_state() reports them from a non-RT thread.
    std::atomic<uint64_t> udptun_egress_reset_count_{0};  ///< total egress anchor resets (watchdog)
    std::atomic<uint64_t> udptun_egress_repunch_count_{
        0};                                   ///< total media-thread re-punches (egress-stuck escalation + tunnel stall)
    std::atomic<uint64_t> udptun_any_rx_{0};  ///< drain: count of ALL datagrams (audio + keepalive)
    std::atomic<int64_t> udptun_last_real_ingest_tai_{0};  ///< RX thread: last real tunnel-audio ingest TAI
    int64_t udptun_last_keepalive_ns_{0};                  ///< media: last keepalive send TAI (rate limit)

    /// Open the UDP socket + resolve the peer + build the ingest/codec from
    /// config_. Called from start(); leaves udptun_enable_ false on any failure
    /// (the entity runs normally without the tunnel). Returns true if armed.
    auto setup_udptun_ingest() -> bool;

    /// Feed received listener audio (raw interleaved 4-byte samples) into the
    /// ingest: anchor TAI on the first packet, reframe to 1 ms, send each. Called
    /// from on_stream_rx_frame for the configured source stream. RX thread.
    /// `real_source` distinguishes genuine listener audio (true) from the
    /// silence-source filler (false): only real audio stamps
    /// udptun_last_real_ingest_tai_, so the silence gate does not suppress itself.
    void udptun_ingest_audio(std::span<uint8_t const> audio, bool real_source = true);

    /// Transcode an AM824 MBLA data block ([label][24-bit] quadlets) to int32 PCM
    /// and ingest it. The tunnel/egress are AAF int32, so the 0x40 label must be
    /// stripped and the 24-bit sample MSB-aligned ([b1][b2][b3][0x00]); sending the
    /// raw quadlets makes the far egress read the label as the sample MSB, yielding
    /// a +0.5 FS DC pedestal + crushed audio (audible distortion). RX thread.
    void udptun_ingest_am824_as_int32(std::span<uint8_t const> mbla);

    /// Direct-peer bidirectional setup: one socket bound to udptun_listen_port
    /// does both ingest TX (sendto peer:port) and egress RX. Used in direct mode
    /// when both ingest and egress are enabled, so both ends transmitting opens
    /// both NAT pinholes without STUN (the owlm direct-peer pattern). Called from
    /// start() in place of the separate ingest/egress setup. Returns true if armed.
    auto setup_udptun_direct_shared() -> bool;

    /// Feed `frames` of zero PCM to the ingest (silence source). Called from the
    /// media timer when udptun_silence_source is set, so the entity transmits
    /// silence as if its listener source were sending zeros. Media-timer thread.
    void udptun_ingest_silence(size_t frames);

    /// Generate `frames` of the logarithmic sweep (config sweep_*) on sweep_channel
    /// (zero on all other channels), encode interleaved int32 (network order, the
    /// tunnel codec's format), and feed it to the ingest as the tunnel source.
    /// Advances the sweep phase. Replaces silence/listener source when sweep_enable.
    void udptun_ingest_sweep(size_t frames);

    /// Feed one reframed inter-site packet (TAI ts + interleaved PCM) to the codec
    /// and send it to the peer (primary), then if redundancy is enabled, replay
    /// the packet from udptun_redun_depth_ sends ago as the redundant copy.
    void udptun_send(int64_t tai_ns, std::span<uint8_t const> pcm);

    /// Encode + send one datagram with an explicit stream_id / sequence / TAI.
    /// Used for both the primary (udptun_stream_id_) and the redundant replay
    /// (udptun_redundant_id_).
    void udptun_send_encoded(ieee::Eui64 const& stream_id, uint32_t sequence, int64_t tai_ns, std::span<uint8_t const> pcm);

    /// Bind the egress UDP socket + build the AudioEgress/decoder from config_.
    /// Called from start(); non-fatal on failure. Returns true if armed.
    auto setup_udptun_egress() -> bool;

    /// STUN-traversed setup: perform the rendezvous (blocks until paired/timeout),
    /// adopt the hole-punched socket as the SHARED udptun socket, and build the
    /// ingest/egress state (per config flags) against the discovered peer. Called
    /// from start() instead of the direct setup when a rendezvous server is set.
    auto setup_udptun_rendezvous() -> bool;

    /// Start the async STUN-rendezvous punch-RETRY worker (when a rendezvous
    /// server is configured). Builds the ingest/egress codec state immediately
    /// (no socket) so the entity's local AVB runs at once, then spawns a worker
    /// thread that hole-punches in a loop -- using a time-windowed session id both
    /// peers derive from their GPS-synced clocks -- and re-punches whenever the
    /// tunnel goes silent. The hole-punched socket is handed to the media thread
    /// via udptun_punch_service(). Non-blocking; returns true if the worker armed.
    auto start_udptun_punch_worker() -> bool;

    /// Worker-thread body: the rendezvous punch-retry loop. Off the hot path.
    void udptun_punch_loop();

    /// Stop + join the punch worker. Idempotent. Called from stop()/dtor.
    void stop_udptun_punch_worker();

    /// Media-thread half of the punch-retry: install a freshly hole-punched socket
    /// staged by the worker (so udptun_fd_ has a single owner), then watchdog the
    /// RX -- if no data arrives within the grace window, or the stream stalls, tear
    /// the socket down and signal the worker to re-punch. `now_tai_ns` = CLOCK_REALTIME
    /// + udptun_tai_offset_ns. Called once per process_audio() wake.
    void udptun_punch_service(int64_t now_tai_ns);

    /// Build the ingest TX state (codec, AudioIngest, tx buffer, flags) -- no
    /// socket. Shared by direct setup_udptun_ingest() and the rendezvous path.
    void build_udptun_ingest_state();

    /// Build the egress RX state (AudioEgress, decoder, buffers, flag) -- no
    /// socket. Shared by direct setup_udptun_egress() and the rendezvous path.
    void build_udptun_egress_state();

    /// The fd the egress drains: the shared socket when ingest TX and egress RX
    /// share one socket (STUN rendezvous OR direct-peer bidirectional), else the
    /// dedicated bound RX socket.
    [[nodiscard]] auto udptun_rx_fd() const noexcept -> int
    {
        return udptun_shared_socket_ ? udptun_fd_.get() : udptun_rx_fd_.get();
    }

    /// Drain the egress UDP socket (non-blocking), decode each datagram, and submit
    /// it to AudioEgress. Runs on the media-timer thread (process_audio).
    void udptun_egress_drain_rx();

    /// Play out `samples` frames of de-tunneled audio for local time `now_tai_ns`
    /// into audio_buffer_ (int32 -> float), replacing the oscillator content;
    /// silence on underrun (drop-to-0). Runs on the media-timer thread.
    void udptun_egress_fill(int64_t now_tai_ns, size_t samples);

    /// Deterministic, GPS-rate-pinned presentation-timestamp generator (shared by
    /// both talker streams -- they emit the same sample count per tick). The
    /// avtp_timestamp comes from this, not the jittery media-timer wake time, so a
    /// listener recovering its media clock from the stream stays rock-steady.
    ptpclient::MediaClockGenerator media_clock_;
    /// GPS frequency ratio r = switch/GPS that pins the media-clock rate, tracked
    /// from CLOCK_REALTIME(GPS) vs gPTP. Updated on the media thread.
    ptpclient::KalmanRatioTracker gps_ratio_{};
    /// Inter-site tunnel timeline: maps the gPTP/PHC master clock to absolute
    /// GPS-TAI (epoch = global GPS-TAI; rate = the gPTP PHC, Kalman-corrected for
    /// the gPTP<->CLOCK_REALTIME offset). The ENTIRE tunnel rides this single
    /// clock -- source pacing, ingest avtp_timestamp, and egress playout -- so both
    /// sites run the identical global rate (no buffer drift) and timestamps are
    /// cross-site comparable. Fed (gptp, CLOCK_REALTIME) by update_gps_ratio.
    ptpclient::GpsTaiTranslator tai_translator_{};
    double r_{1.0};
    uint64_t last_ratio_gptp_ns_{0};
    uint64_t last_ratio_log_ns_{0};  ///< rate-limit for the [media-clock] r/offset-slope telemetry

    /// True if @p frame (AVTP, VLAN tag already stripped) belongs to the stream
    /// currently connected to STREAM_INPUT @p stream_index: the listener must be
    /// connected and the frame's stream_id (offset 4) must match. Rejects other
    /// streams the promiscuous RX socket sees, including our own TX looped back.
    [[nodiscard]] auto frame_is_for_listener(uint16_t stream_index, std::span<uint8_t const> frame) const -> bool;

    /// Update STREAM_INPUT counters for one received stream packet.
    void update_stream_input_counters(
        uint16_t stream_index, uint8_t seq, uint32_t avtp_ts, bool tv, bool tu, bool mr, bool format_ok, uint64_t samples_per_ch);

    /// Fill the GET_COUNTERS bitmap + values for a STREAM_INPUT index (true if it
    /// is one of our stream inputs).
    [[nodiscard]] auto fill_stream_input_counters(uint16_t descriptor_index, uint32_t& valid, std::array<uint32_t, 32>& out) const
        -> bool;

    /// Fill the GET_COUNTERS bitmap + values for a STREAM_OUTPUT (talker) index
    /// (true if it is one of our talker streams). Exposes FRAMES_TX so a reader
    /// can see our actual transmit rate.
    [[nodiscard]] auto fill_stream_output_counters(uint16_t descriptor_index, uint32_t& valid, std::array<uint32_t, 32>& out) const
        -> bool;

    /// Fill the GET_STREAM_INFO response for a STREAM_OUTPUT (talker) index from
    /// the live ACMP stream identity + the descriptor's current_format (true if
    /// it is one of our talker streams). A Milan listener (e.g. the DSP processor) queries
    /// this to verify the stream before sustaining a connection.
    [[nodiscard]] auto fill_stream_output_info(
        uint16_t descriptor_type, uint16_t descriptor_index, atdecc::aem::AemStreamInfoPayload& out) const -> bool;

    bool running_{false};

    //
    // State machines
    //
    nanoavb::supervisor_sm::Machine supervisor_{};
    nanoavb::gptp_sm::Machine gptp_{};
    nanoavb::mvrp_sm::Machine mvrp_{};
    nanoavb::msrp_talker_sm::Machine msrp_talker_{};
    nanoavb::msrp_listener_sm::Machine msrp_listener_{};
    nanoavb::talker_engine_sm::Machine talker_engine_{};
    nanoavb::listener_engine_sm::Machine listener_engine_{};
};

}  // namespace statusbar::avb_entity
