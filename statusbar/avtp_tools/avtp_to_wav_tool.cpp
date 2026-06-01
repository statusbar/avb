// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT
//
// statusbar-avtp-to-wav
//
// Read a pcap/pcapng capture, decode an AVTP AAF stream or a simple
// AM824 MBLA stream, and write a 32-bit-float Broadcast Wave (BWF)
// file with a `bext` chunk. The file is valid plain WAV for any
// consumer that doesn't care about the metadata.
//
// Stream selection: the first matching AVTP audio packet (subtype
// AAF=0x02 or IEC-61883/AM824=0x00) fixes format, channel count, and
// sample rate. Captures containing multiple stream IDs require
// --stream-id=<hex-or-decimal-64> to disambiguate.
//
// Sequence gaps (detected by the stream-input contexts) are reported
// to stderr; no silence is inserted.

#include "statusbar/args/args.hpp"
#include "statusbar/audio/audio_bw64_writer.hpp"
#include "statusbar/avtp/avtp_types.hpp"
#include "statusbar/avtp_tools/avtp_audio_detect.hpp"
#include "statusbar/avtp_tools/avtp_audio_stream_decoder.hpp"
#include "statusbar/config/config.hpp"
#include "statusbar/pcap/pcap.hpp"
#include "statusbar/tsn/tsn_stream_id.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <print>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace statusbar;

struct ToolConfig
{
    std::string input_path;
    std::string output_path;
    std::string stream_id_str;          // "" = auto; otherwise hex or decimal, 64-bit
    std::string stream_format{"auto"};  // auto | aaf | am824
    std::string vlan_id_str;            // "" = any; else 0..=4094
    std::string pcp_str;                // "" = any; else 0..=7
    bool verbose{true};
};

[[nodiscard]] auto parse_hex_or_decimal_u64(std::string_view s, uint64_t& out) -> bool
{
    if (s.empty()) {
        return false;
    }
    int base = 10;
    size_t skip = 0;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        skip = 2;
    }
    out = 0;
    for (size_t i = skip; i < s.size(); ++i) {
        char const c = s[i];
        int digit = -1;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = 10 + (c - 'a');
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = 10 + (c - 'A');
        }
        if (digit < 0 || digit >= base) {
            return false;
        }
        out = out * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
    }
    return true;
}

auto build_arg_specs(ToolConfig& c) -> args::ArgumentSpecs
{
    args::ArgumentSpecs specs;
    specs.add<std::string>("input", "Input pcap or pcapng file", c.input_path, [&](auto v) { c.input_path = v; });
    specs.add<std::string>("output", "Output BWF/WAV file", c.output_path, [&](auto v) { c.output_path = v; });
    specs.add<std::string>(
        "stream-id",
        "64-bit Stream ID to extract (required if capture contains multiple streams). "
        "Accepts decimal or 0x-prefixed hex.",
        c.stream_id_str,
        [&](auto v) { c.stream_id_str = v; });
    specs.add<std::string>(
        "stream-format", "Stream format (auto|aaf|am824); 'am824' decodes simple MBLA audio only", c.stream_format, [&](auto v) {
            c.stream_format = v;
        });
    specs.add<std::string>(
        "vlan-id", "Require this 802.1Q VLAN ID (0..=4094) on matched frames. Empty = accept any.", c.vlan_id_str, [&](auto v) {
            c.vlan_id_str = v;
        });
    specs.add<std::string>(
        "pcp", "Require this 802.1Q Priority Code Point (0..=7) on matched frames. Empty = accept any.", c.pcp_str, [&](auto v) {
            c.pcp_str = v;
        });
    specs.add<bool>("verbose", "Print per-packet and gap diagnostics to stderr", c.verbose, [&](auto v) { c.verbose = v; });
    return specs;
}

[[nodiscard]] auto parse_optional_u16(std::string const& s, uint16_t lo, uint16_t hi, std::optional<uint16_t>& out) -> bool
{
    if (s.empty()) {
        out = std::nullopt;
        return true;
    }
    uint64_t v = 0;
    if (!parse_hex_or_decimal_u64(s, v) || v < lo || v > hi) {
        return false;
    }
    out = static_cast<uint16_t>(v);
    return true;
}

using avtp_tools::AvtpAudioFormat;
using avtp_tools::AvtpAudioSamples;
using avtp_tools::AvtpAudioStreamDecoder;
using avtp_tools::FeedStatus;
using avtp_tools::kind_name;
using avtp_tools::payload_is_aaf;
using avtp_tools::payload_is_am824;
using avtp_tools::stream_id_of;
using avtp_tools::StreamKind;

[[nodiscard]] auto describe_stream_id(uint64_t sid) -> std::string
{
    tsn::StreamId s{};
    s.from_uint64(sid);
    return tsn::to_string(s);
}

// Build a Bw64 description string from the decoder's format info.
[[nodiscard]] auto bw64_description(AvtpAudioFormat const& fmt) -> std::string
{
    if (fmt.kind == StreamKind::aaf) {
        return std::format(
            "statusbar AVTP-to-WAV (AAF): stream_id={} format={} rate={} channels={} bit_depth={}",
            describe_stream_id(fmt.stream_id),
            avtp::aaf_format_name(fmt.aaf_format),
            avtp::aaf_sample_rate_name(fmt.aaf_rate),
            fmt.channel_count,
            fmt.aaf_bit_depth);
    }
    return std::format(
        "statusbar AVTP-to-WAV (AM824-MBLA): stream_id={} rate={} channels={}",
        describe_stream_id(fmt.stream_id),
        avtp::am824_sample_rate_name(fmt.am824_rate),
        fmt.channel_count);
}

[[nodiscard]] auto open_writer_for_format(audio::Bw64Writer& writer, std::string const& path, AvtpAudioFormat const& fmt) -> bool
{
    audio::Bw64Writer::Params params{};
    params.originator = "statusbar-avtp-to-wav";
    params.description = bw64_description(fmt);
    params.sample_rate = fmt.sample_rate_hz;
    params.channel_count = fmt.channel_count;
    params.coding_history =
        std::format("A=PCM,F={},M={}ch,W=32,T=statusbar-avtp-to-wav\r\n", fmt.sample_rate_hz, fmt.channel_count);
    auto const st = writer.open(path, params);
    if (!st) {
        std::print(stderr, "error: cannot open output file {}: {}\n", path, st.error().message());
        return false;
    }
    return true;
}

void log_open(std::string const& path, AvtpAudioFormat const& fmt, std::optional<std::pair<uint16_t, uint8_t>> const& vlan)
{
    if (vlan.has_value()) {
        std::print(
            stderr,
            "opened {}: kind={} rate={}Hz channels={} stream_id={} vlan={} pcp={}\n",
            path,
            kind_name(fmt.kind),
            fmt.sample_rate_hz,
            fmt.channel_count,
            describe_stream_id(fmt.stream_id),
            vlan->first,
            vlan->second);
    } else {
        std::print(
            stderr,
            "opened {}: kind={} rate={}Hz channels={} stream_id={} (untagged)\n",
            path,
            kind_name(fmt.kind),
            fmt.sample_rate_hz,
            fmt.channel_count,
            describe_stream_id(fmt.stream_id));
    }
}

auto run(ToolConfig const& cfg) -> int
{
    if (cfg.stream_format != "auto" && cfg.stream_format != "aaf" && cfg.stream_format != "am824") {
        std::print(stderr, "error: --stream-format must be 'auto', 'aaf', or 'am824'\n");
        return EXIT_FAILURE;
    }

    bool const stream_id_explicit = !cfg.stream_id_str.empty();
    uint64_t explicit_stream_id = 0;
    if (stream_id_explicit && !parse_hex_or_decimal_u64(cfg.stream_id_str, explicit_stream_id)) {
        std::print(stderr, "error: --stream-id='{}' is not a valid 64-bit integer\n", cfg.stream_id_str);
        return EXIT_FAILURE;
    }

    std::optional<uint16_t> require_vlan_id;
    if (!parse_optional_u16(cfg.vlan_id_str, 0, 4094, require_vlan_id)) {
        std::print(stderr, "error: --vlan-id='{}' must be 0..=4094\n", cfg.vlan_id_str);
        return EXIT_FAILURE;
    }
    std::optional<uint16_t> require_pcp_u16;
    if (!parse_optional_u16(cfg.pcp_str, 0, 7, require_pcp_u16)) {
        std::print(stderr, "error: --pcp='{}' must be 0..=7\n", cfg.pcp_str);
        return EXIT_FAILURE;
    }
    std::optional<uint8_t> require_pcp = require_pcp_u16.transform([](uint16_t v) -> uint8_t { return static_cast<uint8_t>(v); });

    auto driver_result = pcap::PcapReplayDriver::open(cfg.input_path);
    if (!driver_result) {
        std::print(stderr, "error: failed to open pcap '{}': {}\n", cfg.input_path, driver_result.error().message());
        return EXIT_FAILURE;
    }
    auto& driver = *driver_result;

    std::set<uint64_t> seen_stream_ids;
    audio::Bw64Writer writer;
    bool writer_open = false;
    AvtpAudioStreamDecoder decoder;
    std::optional<std::pair<uint16_t, uint8_t>> observed_vlan;
    uint64_t non_avtp_frames = 0;
    uint64_t non_audio_avtp_frames = 0;
    uint64_t wrong_vlan_frames = 0;
    uint64_t wrong_stream_id_frames = 0;
    uint64_t wrong_kind_frames = 0;
    bool fatal_error = false;

    if (cfg.verbose) {
        decoder.set_diagnostic([](std::string_view msg) { std::print(stderr, "{}\n", msg); });
    }

    // Sink: lazy-opens the BWF writer on first delivery (after the
    // decoder has populated its format from the first packet) and writes
    // interleaved samples for every subsequent packet.
    decoder.set_sink([&](AvtpAudioSamples const& s) -> bool {
        if (!writer_open) {
            if (!open_writer_for_format(writer, cfg.output_path, decoder.format())) {
                return false;
            }
            writer_open = true;
            if (cfg.verbose) {
                log_open(cfg.output_path, decoder.format(), observed_vlan);
            }
        }
        if (auto const st = writer.write_samples(s.interleaved); !st) {
            std::print(stderr, "error: write_samples failed: {}\n", st.error().message());
            return false;
        }
        return true;
    });

    auto walk_status = driver.for_each([&](pcap::ReplayEvent const& evt) -> bool {
        if (evt.frame.ethertype != avtp::AVTP_ETHERTYPE) {
            ++non_avtp_frames;
            return true;
        }

        // VLAN filter: if --vlan-id or --pcp is set, the frame must
        // actually be 802.1Q-tagged and match.
        if (require_vlan_id.has_value() || require_pcp.has_value()) {
            bool const tagged = evt.frame.vlan_tag.is_set();
            bool const vid_ok = !require_vlan_id.has_value() || (tagged && evt.frame.vlan_tag.get_vid() == *require_vlan_id);
            bool const pcp_ok = !require_pcp.has_value() || (tagged && evt.frame.vlan_tag.get_pcp() == *require_pcp);
            if (!tagged || !vid_ok || !pcp_ok) {
                ++wrong_vlan_frames;
                return true;
            }
        }

        bool const is_aaf = payload_is_aaf(evt.payload) && (cfg.stream_format == "auto" || cfg.stream_format == "aaf");
        bool const is_am824 = payload_is_am824(evt.payload) && (cfg.stream_format == "auto" || cfg.stream_format == "am824");

        if (!is_aaf && !is_am824) {
            ++non_audio_avtp_frames;
            return true;
        }

        StreamKind const packet_kind = is_aaf ? StreamKind::aaf : StreamKind::am824_mbla;
        uint64_t const sid = stream_id_of(evt.payload, packet_kind);
        (void)seen_stream_ids.insert(sid);

        if (stream_id_explicit) {
            if (sid != explicit_stream_id) {
                ++wrong_stream_id_frames;
                return true;
            }
        } else if (seen_stream_ids.size() > 1) {
            std::print(
                stderr,
                "error: multiple AVTP audio stream ids in capture (at least {} and {}); "
                "rerun with --stream-id=<hex-or-decimal-u64>\n",
                describe_stream_id(decoder.format().stream_id),
                describe_stream_id(sid));
            if (writer_open) {
                (void)writer.close();
                std::remove(cfg.output_path.c_str());
            }
            fatal_error = true;
            return false;
        }

        // Capture VLAN info from the very first matching frame, before
        // feed() so the lazy-open log line can include it.
        if (!decoder.initialized() && evt.frame.vlan_tag.is_set()) {
            observed_vlan = std::make_pair(evt.frame.vlan_tag.get_vid(), evt.frame.vlan_tag.get_pcp());
        }

        auto const result = decoder.feed(evt.payload);
        switch (result.status) {
            case FeedStatus::ok:
                break;
            case FeedStatus::wrong_kind:
                // Same stream id but the packet is a different AVTP
                // subtype than the one the decoder locked onto — should
                // not happen in a sane capture; skip and count.
                ++wrong_kind_frames;
                return true;
            case FeedStatus::format_error:
            case FeedStatus::sink_error:
                fatal_error = true;
                return false;
        }
        return true;
    });
    if (!walk_status) {
        std::print(stderr, "error: pcap read failed: {}\n", walk_status.error().message());
        return EXIT_FAILURE;
    }

    if (fatal_error) {
        return EXIT_FAILURE;
    }

    if (writer_open) {
        if (auto const st = writer.close(); !st) {
            std::print(stderr, "error: close failed: {}\n", st.error().message());
            return EXIT_FAILURE;
        }
    }

    auto const& fmt = decoder.format();
    std::print(stderr, "\n=== statusbar-avtp-to-wav summary ===\n");
    std::print(stderr, "input:                   {}\n", cfg.input_path);
    std::print(stderr, "output:                  {}\n", cfg.output_path);
    std::print(stderr, "AVTP audio streams seen: {}\n", seen_stream_ids.size());
    if (decoder.initialized()) {
        std::print(stderr, "decoded stream:          {}\n", describe_stream_id(fmt.stream_id));
        std::print(stderr, "kind/rate/channels:      {} {}Hz {}ch\n", kind_name(fmt.kind), fmt.sample_rate_hz, fmt.channel_count);
        if (observed_vlan.has_value()) {
            std::print(stderr, "observed VLAN:           vid={} pcp={}\n", observed_vlan->first, observed_vlan->second);
        } else {
            std::print(stderr, "observed VLAN:           untagged\n");
        }
        std::print(stderr, "packets decoded:         {}\n", decoder.packets_decoded());
        std::print(stderr, "sample frames written:   {}\n", decoder.sample_frames_delivered());
        std::print(stderr, "sequence gaps observed:  {}\n", decoder.sequence_gaps_observed());
        std::print(stderr, "first gptp pts (ns):     {}\n", decoder.first_gptp_pts_ns());
    } else {
        std::print(stderr, "no matching audio packets.\n");
    }
    std::print(stderr, "skipped (non-AVTP):      {}\n", non_avtp_frames);
    std::print(stderr, "skipped (AVTP non-audio):{}\n", non_audio_avtp_frames);
    std::print(stderr, "skipped (wrong VLAN):    {}\n", wrong_vlan_frames);
    std::print(stderr, "skipped (other stream):  {}\n", wrong_stream_id_frames);
    std::print(stderr, "skipped (kind mismatch): {}\n", wrong_kind_frames);

    return decoder.initialized() ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv)
{
    ToolConfig cfg;
    auto specs = build_arg_specs(cfg);

    auto const status =
        statusbar::config::parse_cli_args(argc, argv, specs, statusbar::config::default_print_usage, "statusbar-avtp-to-wav");
    if (!status) {
        if (statusbar::config::handled_builtin_command(status)) {
            return EXIT_SUCCESS;
        }
        std::print(stderr, "argument error: {}\n", status.error().message());
        return EXIT_FAILURE;
    }

    if (cfg.input_path.empty()) {
        std::print(stderr, "error: --input is required\n");
        return EXIT_FAILURE;
    }
    if (cfg.output_path.empty()) {
        std::print(stderr, "error: --output is required\n");
        return EXIT_FAILURE;
    }

#if __cpp_exceptions
    try {
#endif
        return run(cfg);
#if __cpp_exceptions
    } catch (std::exception const& e) {
        std::print(stderr, "error: {}\n", e.what());
        return EXIT_FAILURE;
    }
#endif
}
