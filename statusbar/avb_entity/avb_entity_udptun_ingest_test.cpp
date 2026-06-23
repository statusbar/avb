// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for the tunnel-ingest AM824->int32 transcode helper
// (avb_entity_udptun_ingest.hpp): drop the MBLA label byte and MSB-align the
// 24-bit audio into an int32 quadlet. Symmetric to the egress deinterleave; the
// buffer-agnostic seam the ingest move (god-object phase 2) writes through.

#include "statusbar/avb_entity/avb_entity_udptun_ingest.hpp"

#include "statusbar/test/test.hpp"

#include <array>
#include <cstdint>
#include <vector>

using statusbar::avb_entity::udptun_am824_mbla_to_int32;

TEST(udptun_ingest, drops_label_and_msb_aligns)
{
    // One MBLA quadlet: label 0x40, audio 0x123456 (big-endian).
    std::array<uint8_t, 4> mbla{0x40, 0x12, 0x34, 0x56};
    std::array<uint8_t, 4> out{0xAA, 0xAA, 0xAA, 0xAA};
    auto const n = udptun_am824_mbla_to_int32(mbla, out);
    EXPECT_EQ(n, size_t{4});
    // int32 = audio << 8 = [0x12][0x34][0x56][0x00]; label is gone.
    EXPECT_EQ(static_cast<int>(out[0]), 0x12);
    EXPECT_EQ(static_cast<int>(out[1]), 0x34);
    EXPECT_EQ(static_cast<int>(out[2]), 0x56);
    EXPECT_EQ(static_cast<int>(out[3]), 0x00);
}

TEST(udptun_ingest, transcodes_multiple_quadlets)
{
    // 3 samples, distinct labels (which must all be discarded).
    std::vector<uint8_t> mbla{0x40, 0x00, 0x00, 0x01, 0x41, 0xFF, 0xFF, 0xFF, 0x42, 0x80, 0x00, 0x00};
    std::vector<uint8_t> out(12, 0xCC);
    auto const n = udptun_am824_mbla_to_int32(mbla, out);
    EXPECT_EQ(n, size_t{12});
    std::array<uint8_t, 12> expect{0x00, 0x00, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x80, 0x00, 0x00, 0x00};
    for (size_t i = 0; i < 12; ++i) {
        EXPECT_EQ(static_cast<int>(out[i]), static_cast<int>(expect[i]));
    }
}

TEST(udptun_ingest, processes_only_whole_quadlets)
{
    // 6 input bytes = 1 whole quadlet + 2 trailing bytes (ignored).
    std::array<uint8_t, 6> mbla{0x40, 0x11, 0x22, 0x33, 0x99, 0x99};
    std::array<uint8_t, 8> out{};
    out.fill(0x7E);
    auto const n = udptun_am824_mbla_to_int32(mbla, out);
    EXPECT_EQ(n, size_t{4});
    EXPECT_EQ(static_cast<int>(out[0]), 0x11);
    EXPECT_EQ(static_cast<int>(out[3]), 0x00);
    EXPECT_EQ(static_cast<int>(out[4]), 0x7E);  // beyond n: untouched
}

TEST(udptun_ingest, bounded_by_smaller_span)
{
    // out only holds 1 quadlet though mbla holds 2 -> stop at out's capacity.
    std::array<uint8_t, 8> mbla{0x40, 0x01, 0x02, 0x03, 0x41, 0x04, 0x05, 0x06};
    std::array<uint8_t, 4> out{};
    auto const n = udptun_am824_mbla_to_int32(mbla, out);
    EXPECT_EQ(n, size_t{4});
    EXPECT_EQ(static_cast<int>(out[0]), 0x01);
}

// Test runner

TEST_MAIN(statusbar_avb_entity, avb_entity_udptun_ingest_test)
