// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/atdecc/atdecc_aem_control_command.hpp"

#include "statusbar/atdecc/atdecc_aem_control_types.hpp"
#include "statusbar/test/test.hpp"

#include <array>

using namespace statusbar;
using namespace statusbar::atdecc::aem;

namespace {

// Helper: make a single-entry linear GAIN descriptor (uint8_t, 0..255 step 1)
auto make_gain_descriptor() -> DescriptorControl
{
    DescriptorControl d{};
    d.descriptor_index = 7;
    d.control_type = CONTROL_TYPE_GAIN;
    LinearValueEntry<uint8_t> e{};
    e.set_minimum(0);
    e.set_maximum(255);
    e.set_step(1);
    e.set_default_value(128);
    e.set_current(100);
    e.unit = ControlUnits{0, UNIT_CODE_DB};
    std::array<LinearValueEntry<uint8_t>, 1> arr{e};
    (void)init_linear<uint8_t>(d, std::span<LinearValueEntry<uint8_t> const>{arr});
    return d;
}

// Helper: make a single-entry linear ATTENUATE (uint16_t, 0..1000 step 10)
auto make_attenuate_descriptor() -> DescriptorControl
{
    DescriptorControl d{};
    d.descriptor_index = 3;
    d.control_type = CONTROL_TYPE_ATTENUATE;
    LinearValueEntry<uint16_t> e{};
    e.set_minimum(0);
    e.set_maximum(1000);
    e.set_step(10);
    e.set_default_value(0);
    e.set_current(50);
    e.unit = ControlUnits{-1, UNIT_CODE_DB};
    std::array<LinearValueEntry<uint16_t>, 1> arr{e};
    (void)init_linear<uint16_t>(d, std::span<LinearValueEntry<uint16_t> const>{arr});
    return d;
}

// Helper: make a selector descriptor for sample-rate choice
auto make_sample_rate_selector() -> DescriptorControl
{
    DescriptorControl d{};
    d.descriptor_index = 0;
    SelectorView<uint32_t> view{};
    view.current = 48000;
    view.default_value = 48000;
    view.options = {44100, 48000, 96000};
    view.unit = ControlUnits{0, UNIT_CODE_HERTZ};
    (void)init_selector<uint32_t>(d, view);
    return d;
}

}  // namespace

TEST(aem_control_command, build_set_control_linear_uint8)
{
    auto d = make_gain_descriptor();

    std::array<uint8_t, 512> buf{};
    auto n = build_set_control_payload(d, buf);
    EXPECT_TRUE(n.has_value());
    // Header (4) + LinearValueEntry<uint8_t>=9 = 13
    EXPECT_EQ(*n, 13U);
    // Header is descriptor_type=CONTROL (0x001A) + descriptor_index=7
    EXPECT_EQ(buf[0], 0x00);
    EXPECT_EQ(buf[1], 0x1A);  // DESCRIPTOR_CONTROL = 0x001A
    EXPECT_EQ(buf[2], 0x00);
    EXPECT_EQ(buf[3], 0x07);
    // Then the entry body: minimum=0, maximum=255, step=1, default=128, current=100
    EXPECT_EQ(buf[4], 0);    // min
    EXPECT_EQ(buf[5], 255);  // max
    EXPECT_EQ(buf[6], 1);    // step
    EXPECT_EQ(buf[7], 128);  // default
    EXPECT_EQ(buf[8], 100);  // current
}

TEST(aem_control_command, build_set_control_buffer_too_small)
{
    auto d = make_gain_descriptor();
    std::array<uint8_t, 4> buf{};  // header-only, no room for body
    auto n = build_set_control_payload(d, buf);
    EXPECT_FALSE(n.has_value());
}

TEST(aem_control_command, parse_response_roundtrip)
{
    auto d = make_gain_descriptor();
    // Update the current value and serialize.
    auto entry = *linear_entry<uint8_t>(d, 0);
    entry.set_current(200);
    std::array<LinearValueEntry<uint8_t>, 1> arr{entry};
    (void)init_linear<uint8_t>(d, std::span<LinearValueEntry<uint8_t> const>{arr});
    std::array<uint8_t, 64> buf{};
    auto n = build_set_control_payload(d, buf);
    EXPECT_TRUE(n.has_value());

    // Parse it back into a fresh descriptor with the same shape.
    DescriptorControl d2{};
    d2.descriptor_index = 7;
    d2.control_value_type = d.control_value_type;
    d2.number_of_values = d.number_of_values;
    auto result = parse_control_response(d2, std::span<uint8_t const>{buf.data(), *n});
    EXPECT_TRUE(result.has_value());
    auto back = linear_entry<uint8_t>(d2, 0);
    EXPECT_TRUE(back.has_value());
    EXPECT_EQ(back->current(), 200U);
}

TEST(aem_control_command, parse_response_rejects_wrong_index)
{
    auto d = make_gain_descriptor();
    std::array<uint8_t, 64> buf{};
    auto n = build_set_control_payload(d, buf);
    EXPECT_TRUE(n.has_value());

    DescriptorControl d2{};
    d2.descriptor_index = 99;  // wrong index
    d2.control_value_type = d.control_value_type;
    d2.number_of_values = d.number_of_values;
    auto result = parse_control_response(d2, std::span<uint8_t const>{buf.data(), *n});
    EXPECT_FALSE(result.has_value());
}

TEST(aem_control_command, validate_linear_in_range)
{
    auto d = make_gain_descriptor();
    EXPECT_TRUE(validate_linear_current<uint8_t>(d, 0, 0));
    EXPECT_TRUE(validate_linear_current<uint8_t>(d, 0, 128));
    EXPECT_TRUE(validate_linear_current<uint8_t>(d, 0, 255));
}

TEST(aem_control_command, validate_linear_out_of_range)
{
    // uint8_t can't overflow into 256, so test with uint16_t instead.
    auto d = make_attenuate_descriptor();
    EXPECT_TRUE(validate_linear_current<uint16_t>(d, 0, 0));
    EXPECT_TRUE(validate_linear_current<uint16_t>(d, 0, 1000));
    EXPECT_FALSE(validate_linear_current<uint16_t>(d, 0, 1001));
    EXPECT_FALSE(validate_linear_current<uint16_t>(d, 0, 65535));
}

TEST(aem_control_command, validate_linear_off_step)
{
    auto d = make_attenuate_descriptor();  // step=10
    EXPECT_TRUE(validate_linear_current<uint16_t>(d, 0, 0));
    EXPECT_TRUE(validate_linear_current<uint16_t>(d, 0, 10));
    EXPECT_TRUE(validate_linear_current<uint16_t>(d, 0, 500));
    EXPECT_FALSE(validate_linear_current<uint16_t>(d, 0, 5));    // not a step
    EXPECT_FALSE(validate_linear_current<uint16_t>(d, 0, 101));  // not a step
}

TEST(aem_control_command, validate_linear_wrong_type_returns_false)
{
    auto d = make_gain_descriptor();                             // uint8_t
    EXPECT_FALSE(validate_linear_current<uint16_t>(d, 0, 100));  // wrong T
}

TEST(aem_control_command, validate_selector_in_options)
{
    auto d = make_sample_rate_selector();
    EXPECT_TRUE(validate_selector_current<uint32_t>(d, 44100));
    EXPECT_TRUE(validate_selector_current<uint32_t>(d, 48000));  // current/default too
    EXPECT_TRUE(validate_selector_current<uint32_t>(d, 96000));
    EXPECT_FALSE(validate_selector_current<uint32_t>(d, 12345));
}

TEST(aem_control_command, build_set_control_linear_current_validates_and_builds)
{
    auto d = make_attenuate_descriptor();
    std::array<uint8_t, 64> buf{};

    // Valid value: step=10, range 0..1000
    auto n = build_set_control_linear_current<uint16_t>(d, 0, 200, buf);
    EXPECT_TRUE(n.has_value());
    // After building, the descriptor reflects the new current value.
    auto back = linear_entry<uint16_t>(d, 0);
    EXPECT_TRUE(back.has_value());
    EXPECT_EQ(back->current(), 200U);

    // Invalid value: off-step
    auto bad = build_set_control_linear_current<uint16_t>(d, 0, 17, buf);
    EXPECT_FALSE(bad.has_value());

    // Invalid value: above max
    auto too_high = build_set_control_linear_current<uint16_t>(d, 0, 2000, buf);
    EXPECT_FALSE(too_high.has_value());
}

TEST(aem_control_command, build_set_control_selector_current_validates_and_builds)
{
    auto d = make_sample_rate_selector();
    std::array<uint8_t, 64> buf{};

    auto n = build_set_control_selector_current<uint32_t>(d, 96000, buf);
    EXPECT_TRUE(n.has_value());
    auto back = selector_view<uint32_t>(d);
    EXPECT_TRUE(back.has_value());
    EXPECT_EQ(back->current, 96000U);

    auto bad = build_set_control_selector_current<uint32_t>(d, 11025, buf);
    EXPECT_FALSE(bad.has_value());
}

TEST_MAIN(statusbar_atdecc, atdecc_aem_control_command_test)
