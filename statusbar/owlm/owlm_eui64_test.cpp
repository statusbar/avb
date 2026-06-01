// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/owlm/owlm_eui64.hpp"

#include "statusbar/test/test.hpp"

#include <array>
#include <cstdint>

namespace {

using namespace statusbar;
using namespace statusbar::owlm;

auto bytes_of(ieee::Eui64 const& e) -> std::array<uint8_t, 8>
{
    std::array<uint8_t, 8> out{};
    auto const s = e.span();
    for (size_t i = 0; i < 8; ++i) {
        out[i] = s[i];
    }
    return out;
}

TEST(owlm_eui64, default_mid_ff_fe)
{
    ieee::Eui48 const mac{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    auto const eui = make_owlm_eui64(mac, 0xFFFE);
    std::array<uint8_t, 8> const expected{0xAA, 0xBB, 0xCC, 0xFF, 0xFE, 0xDD, 0xEE, 0xFF};
    auto const got = bytes_of(eui);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(got[i], expected[i]);
    }
}

TEST(owlm_eui64, custom_mid_value)
{
    ieee::Eui48 const mac{0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    auto const eui = make_owlm_eui64(mac, 0x1234);
    std::array<uint8_t, 8> const expected{0x01, 0x02, 0x03, 0x12, 0x34, 0x04, 0x05, 0x06};
    auto const got = bytes_of(eui);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(got[i], expected[i]);
    }
}

TEST(owlm_eui64, mid_zero)
{
    ieee::Eui48 const mac{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    auto const eui = make_owlm_eui64(mac, 0x0000);
    std::array<uint8_t, 8> const expected{0xAA, 0xBB, 0xCC, 0x00, 0x00, 0xDD, 0xEE, 0xFF};
    auto const got = bytes_of(eui);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(got[i], expected[i]);
    }
}

TEST(owlm_eui64, ul_bit_not_flipped)
{
    // First octet 0x02 has the U/L bit set. Modified-EUI-64 (RFC 4291)
    // would flip it to 0x00. We do NOT flip — first byte is preserved.
    ieee::Eui48 const mac{0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    auto const eui = make_owlm_eui64(mac, 0xFFFE);
    auto const got = bytes_of(eui);
    EXPECT_EQ(got[0], uint8_t{0x02});
}

}  // namespace

TEST_MAIN(statusbar_owlm, owlm_eui64_test)
