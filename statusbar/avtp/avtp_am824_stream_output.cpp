// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_am824_stream_output.hpp"

namespace statusbar::avtp {

Am824StreamOutputContext::Am824StreamOutputContext(StreamId sid, Am824SampleRate rate, uint8_t channels, uint64_t pres_offset)
    : config{rate, channels}
    , stream_id{sid}
    , presentation_offset_ns{pres_offset}
{}

void Am824StreamOutputContext::reset() noexcept
{
    sequence_num = 0;
    running_dbc = 0;
    packets_sent = 0;
    timestamp_inserts = 0;
}

void Am824StreamOutputContext::build_packet_header(Am824Pdu& pdu, uint8_t samples_per_channel, uint64_t gptp_now_ns) noexcept
{
    pdu.set_stream_id(stream_id);
    pdu.set_sequence_num(sequence_num);
    pdu.set_dimensions(samples_per_channel, config.channel_count);
    pdu.set_sample_rate(config.sample_rate);
    pdu.set_data_block_count(static_cast<uint8_t>(running_dbc & 0xFFU));

    // Determine if we need to insert a timestamp in this packet.
    // IEEE 1722-2016 Section 5.4: tv=1 when the packet contains the data block
    // at the syt_interval boundary.
    //
    // We set tv=1 if any data block in this packet falls on a syt_interval boundary.
    // The AVTP timestamp is the presentation time of that data block.
    bool tv = false;
    uint32_t ts_dbc = 0;

    if (config.syt_interval > 0) {
        // Check each data block in this packet
        for (uint8_t s = 0; s < samples_per_channel; ++s) {
            uint32_t const dbc = running_dbc + s;
            if ((dbc % config.syt_interval) == 0) {
                tv = true;
                ts_dbc = dbc;
                break;  // use the first boundary in this packet
            }
        }
    }

    if (tv) {
        // Presentation time = current gPTP time + presentation offset,
        // adjusted to the exact data block position.
        // The base presentation time is for running_dbc; ts_dbc may be offset.
        uint64_t const base_pts = gptp_now_ns + presentation_offset_ns;
        int64_t const dbc_offset = static_cast<int64_t>(ts_dbc) - static_cast<int64_t>(running_dbc);
        uint64_t const pts = base_pts + static_cast<uint64_t>(dbc_offset * static_cast<int64_t>(config.sample_period_ns));

        // Store lower 32 bits as the AVTP timestamp, and the same presentation
        // time as the CIP SYT (61883 cycle-time). Without a valid (non-0xFFFF)
        // SYT, receivers treat the packet as carrying no timing and cannot
        // recover the media clock -> periodic sample slip / audible click.
        pdu.set_tv(true);
        pdu.set_avtp_timestamp(static_cast<uint32_t>(pts & 0xFFFF'FFFFU));
        pdu.set_syt_timestamp(am824_presentation_to_syt(pts));
        ++timestamp_inserts;
    } else {
        // No syt_interval boundary in this packet: no timestamp this packet.
        pdu.set_tv(false);
        pdu.set_avtp_timestamp(0);
        pdu.set_syt_timestamp(0xFFFFU);
    }

    // Advance state
    running_dbc += samples_per_channel;
    sequence_num = static_cast<uint8_t>((sequence_num + 1U) & 0xFFU);
    ++packets_sent;
}

}  // namespace statusbar::avtp
