// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/atdecc/atdecc_aem_control_accessors.hpp"

#include "statusbar/atdecc/atdecc_aem_control_types.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <span>
#include <string_view>

using namespace statusbar;
using namespace statusbar::atdecc::aem;

TEST(aem_control_accessors, value_details_length_linear)
{
    DescriptorControl d{};
    d.control_value_type = pack_control_value_type(false, false, CONTROL_LINEAR_UINT8);
    d.number_of_values = 1;
    EXPECT_EQ(d.value_details_length(), 9U);  // T = 5*1 + 4
    d.number_of_values = 2;
    EXPECT_EQ(d.value_details_length(), 18U);
}

TEST(aem_control_accessors, value_details_length_smpte)
{
    DescriptorControl d{};
    d.control_value_type = pack_control_value_type(false, false, CONTROL_SMPTE_TIME);
    d.number_of_values = 1;
    EXPECT_EQ(d.value_details_length(), 10U);
}

TEST(aem_control_accessors, linear_init_and_read_back_uint8_gain)
{
    DescriptorControl d{};
    d.control_type = CONTROL_TYPE_GAIN;

    LinearValueEntry<uint8_t> e{};
    e.set_minimum(0);
    e.set_maximum(255);
    e.set_step(1);
    e.set_default_value(128);
    e.set_current(200);
    e.unit = ControlUnits{0, UNIT_CODE_DB};

    std::array<LinearValueEntry<uint8_t>, 1> entries{e};
    EXPECT_TRUE(init_linear<uint8_t>(d, std::span<LinearValueEntry<uint8_t> const>{entries}));
    EXPECT_EQ(d.number_of_values.get(), 1U);
    EXPECT_EQ(d.values_offset.get(), DescriptorControl::LENGTH);

    auto back = linear_entry<uint8_t>(d, 0);
    EXPECT_TRUE(back.has_value());
    EXPECT_EQ(back->minimum(), 0U);
    EXPECT_EQ(back->maximum(), 255U);
    EXPECT_EQ(back->default_value(), 128U);
    EXPECT_EQ(back->current(), 200U);
    EXPECT_EQ(back->unit.code.get(), UNIT_CODE_DB);
}

TEST(aem_control_accessors, linear_wrong_type_returns_nullopt)
{
    DescriptorControl d{};
    d.control_value_type = pack_control_value_type(false, false, CONTROL_LINEAR_UINT16);
    d.number_of_values = 1;
    auto back = linear_entry<uint8_t>(d, 0);
    EXPECT_FALSE(back.has_value());
}

TEST(aem_control_accessors, linear_init_capacity_limit)
{
    DescriptorControl d{};
    // LinearValueEntry<uint64_t> = 44 bytes each; 404 / 44 = 9 max.
    std::array<LinearValueEntry<uint64_t>, 10> too_many{};
    EXPECT_FALSE(init_linear<uint64_t>(d, std::span<LinearValueEntry<uint64_t> const>{too_many}));
    std::array<LinearValueEntry<uint64_t>, 9> fits{};
    EXPECT_TRUE(init_linear<uint64_t>(d, std::span<LinearValueEntry<uint64_t> const>{fits}));
}

TEST(aem_control_accessors, linear_int16_negative_roundtrip)
{
    DescriptorControl d{};
    LinearValueEntry<int16_t> e{};
    e.set_minimum(-1000);
    e.set_maximum(1000);
    e.set_step(10);
    e.set_default_value(0);
    e.set_current(-250);
    std::array<LinearValueEntry<int16_t>, 1> entries{e};
    EXPECT_TRUE(init_linear<int16_t>(d, std::span<LinearValueEntry<int16_t> const>{entries}));
    auto back = linear_entry<int16_t>(d, 0);
    EXPECT_TRUE(back.has_value());
    EXPECT_EQ(back->minimum(), -1000);
    EXPECT_EQ(back->current(), -250);
}

TEST(aem_control_accessors, selector_roundtrip)
{
    DescriptorControl d{};
    SelectorView<uint32_t> view{};
    view.current = 48000;
    view.default_value = 48000;
    view.options = {44100, 48000, 88200, 96000};
    view.unit = ControlUnits{0, UNIT_CODE_HERTZ};

    EXPECT_TRUE(init_selector<uint32_t>(d, view));
    EXPECT_EQ(d.number_of_values.get(), 4U);

    auto back = selector_view<uint32_t>(d);
    EXPECT_TRUE(back.has_value());
    EXPECT_EQ(back->current, 48000U);
    EXPECT_EQ(back->default_value, 48000U);
    EXPECT_EQ(back->options.size(), 4U);
    EXPECT_EQ(back->options[2], 88200U);
    EXPECT_EQ(back->unit.code.get(), UNIT_CODE_HERTZ);
}

TEST(aem_control_accessors, array_roundtrip)
{
    DescriptorControl d{};
    ArrayView<int32_t> view{};
    view.minimum = -100;
    view.maximum = 100;
    view.step = 1;
    view.default_value = 0;
    view.unit = ControlUnits{-1, UNIT_CODE_DB};
    view.localized_string = 0xFFFF;
    view.current = {-10, 0, 10, 20, 30};

    EXPECT_TRUE(init_array<int32_t>(d, view));
    EXPECT_EQ(d.number_of_values.get(), 5U);

    auto back = array_view<int32_t>(d);
    EXPECT_TRUE(back.has_value());
    EXPECT_EQ(back->minimum, -100);
    EXPECT_EQ(back->maximum, 100);
    EXPECT_EQ(back->current.size(), 5U);
    EXPECT_EQ(back->current[2], 10);
    EXPECT_EQ(back->unit.signed_multiplier(), -1);
}

TEST(aem_control_accessors, utf8_roundtrip)
{
    DescriptorControl d{};
    EXPECT_TRUE(init_utf8(d, "Hello world"));
    auto back = control_utf8(d);
    EXPECT_TRUE(back.has_value());
    EXPECT_EQ(*back, std::string_view{"Hello world"});
}

TEST(aem_control_accessors, utf8_length_cap)
{
    DescriptorControl d{};
    std::string big(DescriptorControl::MAX_VALUE_DETAILS, 'a');
    EXPECT_FALSE(init_utf8(d, big));  // no room for NUL
    big.pop_back();
    EXPECT_TRUE(init_utf8(d, big));
}

TEST(aem_control_accessors, smpte_time_roundtrip)
{
    DescriptorControl d{};
    SmpteTimeValue t{};
    t.hours = 12;
    t.minutes = 34;
    t.seconds = 56;
    t.frames = 24;
    t.frames_per_second = 30;
    init_smpte_time(d, t);
    auto back = control_smpte_time(d);
    EXPECT_TRUE(back.has_value());
    EXPECT_EQ(back->hours.get(), 12U);
    EXPECT_EQ(back->minutes.get(), 34U);
    EXPECT_EQ(back->frames_per_second.get(), 30U);
}

TEST(aem_control_accessors, sample_rate_roundtrip)
{
    DescriptorControl d{};
    SampleRateValue r{};
    r.current = 48000;
    r.default_value = 48000;
    r.minimum = 44100;
    r.maximum = 96000;
    init_sample_rate(d, r);
    auto back = control_sample_rate(d);
    EXPECT_TRUE(back.has_value());
    EXPECT_EQ(back->current.get(), 48000U);
    EXPECT_EQ(back->maximum.get(), 96000U);
}

TEST(aem_control_accessors, gptp_time_roundtrip)
{
    DescriptorControl d{};
    GptpTimeValue g{};
    g.set_seconds(0x1234567890ULL);
    g.gptp_nanoseconds = 999'999'999;
    init_gptp_time(d, g);
    auto back = control_gptp_time(d);
    EXPECT_TRUE(back.has_value());
    EXPECT_EQ(back->seconds(), 0x1234567890ULL);
    EXPECT_EQ(back->gptp_nanoseconds.get(), 999'999'999U);
}

TEST(aem_control_accessors, bode_plot_roundtrip)
{
    DescriptorControl d{};
    BodePlotHeader h{};
    h.frequency_minimum = 0x42480000;  // 50.0 as float bits
    std::array<BodePlotPoint, 2> pts{};
    pts[0].frequency = 0x449A4000;  // 1234.0f bits
    pts[1].frequency = 0x4A741894;  // ~4000000 bits
    EXPECT_TRUE(init_bode_plot(d, h, std::span<BodePlotPoint const>{pts}));
    EXPECT_EQ(d.number_of_values.get(), 2U);
    auto back_h = control_bode_plot_header(d);
    EXPECT_TRUE(back_h.has_value());
    EXPECT_EQ(back_h->frequency_minimum.get(), 0x42480000U);
    auto back_p0 = control_bode_plot_point(d, 0);
    EXPECT_TRUE(back_p0.has_value());
    EXPECT_EQ(back_p0->frequency.get(), 0x449A4000U);
    auto back_p1 = control_bode_plot_point(d, 1);
    EXPECT_TRUE(back_p1.has_value());
    auto out_of_range = control_bode_plot_point(d, 2);
    EXPECT_FALSE(out_of_range.has_value());
}

TEST(aem_control_accessors, vendor_blob_roundtrip)
{
    DescriptorControl d{};
    std::array<uint8_t, 5> blob{0xDE, 0xAD, 0xBE, 0xEF, 0x42};
    EXPECT_TRUE(init_vendor(d, std::span<uint8_t const>{blob}));
    auto back = control_vendor(d);
    EXPECT_TRUE(back.has_value());
    EXPECT_EQ((*back)[0], 0xDEU);
    EXPECT_EQ((*back)[4], 0x42U);
}

TEST(aem_control_accessors, wire_size_for_linear_descriptor)
{
    DescriptorControl d{};
    LinearValueEntry<uint16_t> e{};
    e.set_maximum(0xFFFF);
    std::array<LinearValueEntry<uint16_t>, 3> arr{e, e, e};
    EXPECT_TRUE(init_linear<uint16_t>(d, std::span<LinearValueEntry<uint16_t> const>{arr}));
    // Fixed 104 + 3 * (5*2+4) = 104 + 42 = 146
    EXPECT_EQ(d.wire_size(), 146U);
    EXPECT_EQ(d.value_details_bytes().size(), 42U);
}

TEST_MAIN(statusbar_atdecc, atdecc_aem_control_accessors_test)
