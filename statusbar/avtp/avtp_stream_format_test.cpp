// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_stream_format.hpp"

#include "statusbar/test/test.hpp"

#include <cstdint>
#include <string>

using namespace statusbar;
using namespace statusbar::avtp;

// AAF: rate=48000 (nsr=0x05), channels=2, depth=24
// Byte layout: 02 50 02 18 00 00 00 00 → 0x0250021800000000
TEST(stream_format, aaf_48khz_2ch_24bit)
{
    auto const s = stream_format_to_string(0x0250021800000000ULL);
    EXPECT_TRUE(s == "AAF 2ch 48kHz 24-bit");
}

TEST(stream_format, aaf_96khz_8ch_32bit)
{
    auto const s = stream_format_to_string(0x0270082000000000ULL);
    EXPECT_TRUE(s == "AAF 8ch 96kHz 32-bit");
}

TEST(stream_format, aaf_441khz_2ch_16bit)
{
    auto const s = stream_format_to_string(0x0240021000000000ULL);
    EXPECT_TRUE(s == "AAF 2ch 44.1kHz 16-bit");
}

// AM824/IEC 61883-6 real-world format from the audio interface: 0x00a0020840000800
// Byte 2 = 0x02 = SFC for 48 kHz (whole byte)
// Byte 3 = 0x08 = 8 channels (DBS)
TEST(stream_format, am824_audio_iface_is_8ch_48khz)
{
    auto const s = stream_format_to_string(0x00a0020840000800ULL);
    EXPECT_TRUE(s == "AM824 8ch 48kHz");
}

// AM824 at 96 kHz, 2 channels
// Byte 2 = 0x04 (SFC for 96 kHz), Byte 3 = 0x02 (DBS = 2)
TEST(stream_format, am824_2ch_96khz)
{
    auto const s = stream_format_to_string(0x00a0040200000000ULL);
    EXPECT_TRUE(s == "AM824 2ch 96kHz");
}

TEST(stream_format, unknown_subtype_falls_back_to_hex_only)
{
    auto const s = stream_format_to_string(0x1234567890ABCDEFULL);
    EXPECT_TRUE(s == "0x1234567890abcdef");
}

TEST(stream_format, crf_formatted_as_crf)
{
    auto const s = stream_format_to_string(0x0400000000000000ULL);
    EXPECT_TRUE(s == "CRF");
}

TEST(stream_format, aaf_unknown_rate_shows_nsr_code)
{
    auto const s = stream_format_to_string(0x02F0011000000000ULL);
    EXPECT_TRUE(s.find("rate?") != std::string::npos);
}

TEST_MAIN(statusbar_avtp, avtp_stream_format_test)
