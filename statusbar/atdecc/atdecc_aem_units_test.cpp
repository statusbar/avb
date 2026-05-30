// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/atdecc/atdecc_aem_units.hpp"

#include "statusbar/test/test.hpp"

#include <cstring>

using namespace statusbar;
using namespace statusbar::atdecc::aem;

TEST(aem_units, wire_size_is_two)
{
    EXPECT_EQ(sizeof(ControlUnits), 2U);
}

TEST(aem_units, construct_from_bytes_roundtrips)
{
    ControlUnits u{-1, UNIT_CODE_DB};
    EXPECT_EQ(u.signed_multiplier(), -1);
    EXPECT_EQ(u.code.get(), UNIT_CODE_DB);
}

TEST(aem_units, group_lookup)
{
    EXPECT_EQ(unit_code_group(UNIT_CODE_HERTZ), UNIT_GROUP_FREQUENCY);
    EXPECT_EQ(unit_code_group(UNIT_CODE_SECONDS), UNIT_GROUP_TIME);
    EXPECT_EQ(unit_code_group(UNIT_CODE_DB_A), UNIT_GROUP_LEVELS);
    EXPECT_EQ(unit_code_group(UNIT_CODE_UNITLESS), UNIT_GROUP_UNITLESS);
    EXPECT_EQ(unit_code_group(UNIT_CODE_KELVIN), UNIT_GROUP_TEMPERATURE);
}

TEST(aem_units, name_lookup_named_codes)
{
    EXPECT_EQ(std::strcmp(control_unit_code_name(UNIT_CODE_UNITLESS), "UNITLESS"), 0);
    EXPECT_EQ(std::strcmp(control_unit_code_name(UNIT_CODE_HERTZ), "HERTZ"), 0);
    EXPECT_EQ(std::strcmp(control_unit_code_name(UNIT_CODE_DB), "DB"), 0);
    EXPECT_EQ(std::strcmp(control_unit_code_name(UNIT_CODE_DB_SPL_A), "DB_SPL_A"), 0);
    EXPECT_EQ(std::strcmp(control_unit_code_name(UNIT_CODE_METRES_PER_SEC_SQUARED), "METRES_PER_SEC_SQUARED"), 0);
    EXPECT_EQ(std::strcmp(control_unit_code_name(UNIT_CODE_LUFS), "LUFS"), 0);
}

TEST(aem_units, name_lookup_reserved_codes)
{
    // 0x04 is reserved in the unitless group (only 0x00-0x03 defined).
    EXPECT_EQ(std::strcmp(control_unit_code_name(0x04), "Reserved"), 0);
    // 0xc0 and above are the reserved high range.
    EXPECT_EQ(std::strcmp(control_unit_code_name(0xC0), "Reserved (>= 0xc0)"), 0);
    EXPECT_EQ(std::strcmp(control_unit_code_name(0xFF), "Reserved (>= 0xc0)"), 0);
}

TEST(aem_units, suffix_lookup)
{
    EXPECT_EQ(std::strcmp(control_unit_code_suffix(UNIT_CODE_HERTZ), "Hz"), 0);
    EXPECT_EQ(std::strcmp(control_unit_code_suffix(UNIT_CODE_DB), "dB"), 0);
    EXPECT_EQ(std::strcmp(control_unit_code_suffix(UNIT_CODE_PERCENT), "%"), 0);
    EXPECT_EQ(std::strcmp(control_unit_code_suffix(UNIT_CODE_SECONDS), "s"), 0);
    EXPECT_EQ(std::strcmp(control_unit_code_suffix(UNIT_CODE_DB_SPL_A), "dB(A) SPL"), 0);
    // Unitless codes have no suffix.
    EXPECT_EQ(std::strcmp(control_unit_code_suffix(UNIT_CODE_UNITLESS), ""), 0);
    EXPECT_EQ(std::strcmp(control_unit_code_suffix(UNIT_CODE_COUNT), ""), 0);
    // Reserved codes have no suffix.
    EXPECT_EQ(std::strcmp(control_unit_code_suffix(0xC0), ""), 0);
}

TEST_MAIN(statusbar_atdecc, atdecc_aem_units_test)
