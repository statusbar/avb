// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp.hpp"
#include "statusbar/buffer/buffer.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/status/status.hpp"
#include "statusbar/test/test.hpp"
#include "statusbar/tsn/tsn.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

using namespace statusbar;
using namespace statusbar::avtp;
using namespace statusbar::tsn;
using namespace statusbar::ieee;

//
// Compile-time verification of constexpr functions using static_assert
//

// CRF type names - tested at runtime (non-constexpr)

// CRF pull names - tested at runtime (non-constexpr)

// CRF frequency calculation
static_assert(crf_calculate_frequency(48000, CRF_PULL_MULT_1_0) == 48000.0);
static_assert(crf_calculate_frequency(48000, CRF_PULL_MULT_1_DIV_8) == 6000.0);
static_assert(crf_calculate_frequency(48000, CrfPull::multiply_1_0) == 48000.0);

//
// Name function tests (runtime, non-constexpr)
//

TEST(crf_names, type_name_uint8)
{
    EXPECT_EQ(crf_type_name(CRF_TYPE_USER)[0], 'U');
    EXPECT_EQ(crf_type_name(CRF_TYPE_AUDIO_SAMPLE)[0], 'A');
    EXPECT_EQ(crf_type_name(CRF_TYPE_VIDEO_FRAME)[0], 'V');
    EXPECT_EQ(crf_type_name(CRF_TYPE_VIDEO_LINE)[0], 'V');
    EXPECT_EQ(crf_type_name(CRF_TYPE_MACHINE_CYCLE)[0], 'M');
    EXPECT_EQ(crf_type_name(0xFF)[0], 'R');
}

TEST(crf_names, type_name_enum)
{
    EXPECT_EQ(crf_type_name(CrfType::user)[0], 'U');
    EXPECT_EQ(crf_type_name(CrfType::audio_sample)[0], 'A');
    EXPECT_EQ(crf_type_name(CrfType::video_frame)[0], 'V');
}

TEST(crf_names, pull_name_uint8)
{
    EXPECT_EQ(crf_pull_name(CRF_PULL_MULT_1_0)[0], '1');
    EXPECT_EQ(crf_pull_name(CRF_PULL_MULT_1_DIV_1001)[0], '1');
    EXPECT_EQ(crf_pull_name(CRF_PULL_MULT_1001)[0], '1');
    EXPECT_EQ(crf_pull_name(CRF_PULL_MULT_24_DIV_25)[0], '2');
    EXPECT_EQ(crf_pull_name(CRF_PULL_MULT_25_DIV_24)[0], '2');
    EXPECT_EQ(crf_pull_name(CRF_PULL_MULT_1_DIV_8)[0], '1');
    EXPECT_EQ(crf_pull_name(0x07)[0], 'R');
}

TEST(crf_names, pull_name_enum)
{
    EXPECT_EQ(crf_pull_name(CrfPull::multiply_1_0)[0], '1');
    EXPECT_EQ(crf_pull_name(CrfPull::multiply_1_div_1001)[0], '1');
}

//
// Tests: CRF Type Constants
//

TEST(crf_constants, subtype)
{
    EXPECT_EQ(AvtpSubtype::crf, 0x04);
}

TEST(crf_constants, types)
{
    EXPECT_EQ(CRF_TYPE_USER, 0x00);
    EXPECT_EQ(CRF_TYPE_AUDIO_SAMPLE, 0x01);
    EXPECT_EQ(CRF_TYPE_VIDEO_FRAME, 0x02);
    EXPECT_EQ(CRF_TYPE_VIDEO_LINE, 0x03);
    EXPECT_EQ(CRF_TYPE_MACHINE_CYCLE, 0x04);
}

TEST(crf_constants, pull_values)
{
    EXPECT_EQ(CRF_PULL_MULT_1_0, 0x00);
    EXPECT_EQ(CRF_PULL_MULT_1_DIV_1001, 0x01);
    EXPECT_EQ(CRF_PULL_MULT_1001, 0x02);
    EXPECT_EQ(CRF_PULL_MULT_24_DIV_25, 0x03);
    EXPECT_EQ(CRF_PULL_MULT_25_DIV_24, 0x04);
    EXPECT_EQ(CRF_PULL_MULT_1_DIV_8, 0x05);
}

//
// Tests: CRF Type Names
//

TEST(crf_type_names, user)
{
    EXPECT_TRUE(std::string(crf_type_name(CRF_TYPE_USER)) == "User");
    EXPECT_TRUE(std::string(crf_type_name(CrfType::user)) == "User");
}

TEST(crf_type_names, audio_sample)
{
    EXPECT_TRUE(std::string(crf_type_name(CRF_TYPE_AUDIO_SAMPLE)) == "Audio Sample");
    EXPECT_TRUE(std::string(crf_type_name(CrfType::audio_sample)) == "Audio Sample");
}

TEST(crf_type_names, video_frame)
{
    EXPECT_TRUE(std::string(crf_type_name(CRF_TYPE_VIDEO_FRAME)) == "Video Frame");
    EXPECT_TRUE(std::string(crf_type_name(CrfType::video_frame)) == "Video Frame");
}

TEST(crf_type_names, video_line)
{
    EXPECT_TRUE(std::string(crf_type_name(CRF_TYPE_VIDEO_LINE)) == "Video Line");
    EXPECT_TRUE(std::string(crf_type_name(CrfType::video_line)) == "Video Line");
}

TEST(crf_type_names, machine_cycle)
{
    EXPECT_TRUE(std::string(crf_type_name(CRF_TYPE_MACHINE_CYCLE)) == "Machine Cycle");
    EXPECT_TRUE(std::string(crf_type_name(CrfType::machine_cycle)) == "Machine Cycle");
}

TEST(crf_type_names, reserved)
{
    EXPECT_TRUE(std::string(crf_type_name(0xFF)) == "Reserved");
}

//
// Tests: CRF Pull Names
//

TEST(crf_pull_names, all_values)
{
    EXPECT_TRUE(std::string(crf_pull_name(CRF_PULL_MULT_1_0)) == "1.0");
    EXPECT_TRUE(std::string(crf_pull_name(CRF_PULL_MULT_1_DIV_1001)) == "1/1.001");
    EXPECT_TRUE(std::string(crf_pull_name(CRF_PULL_MULT_1001)) == "1.001");
    EXPECT_TRUE(std::string(crf_pull_name(CRF_PULL_MULT_24_DIV_25)) == "24/25");
    EXPECT_TRUE(std::string(crf_pull_name(CRF_PULL_MULT_25_DIV_24)) == "25/24");
    EXPECT_TRUE(std::string(crf_pull_name(CRF_PULL_MULT_1_DIV_8)) == "1/8");
    EXPECT_TRUE(std::string(crf_pull_name(0x07)) == "Reserved");
}

TEST(crf_pull_names, enum_values)
{
    EXPECT_TRUE(std::string(crf_pull_name(CrfPull::multiply_1_0)) == "1.0");
    EXPECT_TRUE(std::string(crf_pull_name(CrfPull::multiply_1_div_1001)) == "1/1.001");
    EXPECT_TRUE(std::string(crf_pull_name(CrfPull::multiply_1001)) == "1.001");
}

//
// Tests: CRF Frequency Calculation
//

TEST(crf_frequency, multiply_1_0)
{
    EXPECT_TRUE(crf_calculate_frequency(48000, CRF_PULL_MULT_1_0) == 48000.0);
    EXPECT_TRUE(crf_calculate_frequency(48000, CrfPull::multiply_1_0) == 48000.0);
}

TEST(crf_frequency, multiply_1_div_1001)
{
    double const expected = 48000.0 / 1.001;
    double const result = crf_calculate_frequency(48000, CRF_PULL_MULT_1_DIV_1001);
    EXPECT_TRUE(result > expected - 0.001 && result < expected + 0.001);
}

TEST(crf_frequency, multiply_1001)
{
    double const expected = 48000.0 * 1.001;
    double const result = crf_calculate_frequency(48000, CRF_PULL_MULT_1001);
    EXPECT_TRUE(result > expected - 0.001 && result < expected + 0.001);
}

TEST(crf_frequency, multiply_24_div_25)
{
    double const expected = 48000.0 * 24.0 / 25.0;
    double const result = crf_calculate_frequency(48000, CRF_PULL_MULT_24_DIV_25);
    EXPECT_TRUE(result > expected - 0.001 && result < expected + 0.001);
}

TEST(crf_frequency, multiply_25_div_24)
{
    double const expected = 48000.0 * 25.0 / 24.0;
    double const result = crf_calculate_frequency(48000, CRF_PULL_MULT_25_DIV_24);
    EXPECT_TRUE(result > expected - 0.001 && result < expected + 0.001);
}

TEST(crf_frequency, multiply_1_div_8)
{
    EXPECT_TRUE(crf_calculate_frequency(48000, CRF_PULL_MULT_1_DIV_8) == 6000.0);
}

//
// Tests: CrfPdu Structure
//

TEST(crfpdu_struct, size)
{
    EXPECT_EQ(sizeof(CrfPdu), 20U);
    EXPECT_EQ(CrfPdu::HEADER_LENGTH, 20U);
    EXPECT_EQ(CrfPdu::TIMESTAMP_SIZE, 8U);
}

TEST(crfpdu_struct, default_constructor)
{
    CrfPdu crf;
    EXPECT_EQ(crf.subtype.get(), 0);
    EXPECT_EQ(crf.get_sequence_num(), 0);
    EXPECT_EQ(crf.get_type(), 0);
    EXPECT_EQ(crf.base_frequency(), 0U);
    EXPECT_EQ(crf.crf_data_length(), 0);
    EXPECT_EQ(crf.timestamp_interval(), 0);
}

//
// Tests: CrfPdu Header Field Accessors
//

TEST(crfpdu_fields, sv_bit)
{
    CrfPdu crf;
    EXPECT_FALSE(crf.sv());

    crf.set_sv(true);
    EXPECT_TRUE(crf.sv());

    crf.set_sv(false);
    EXPECT_FALSE(crf.sv());
}

TEST(crfpdu_fields, version)
{
    CrfPdu crf;
    crf.set_version(5);
    EXPECT_EQ(crf.version(), 5);

    crf.set_version(7);
    EXPECT_EQ(crf.version(), 7);

    crf.set_version(0);
    EXPECT_EQ(crf.version(), 0);
}

TEST(crfpdu_fields, mr_bit)
{
    CrfPdu crf;
    EXPECT_FALSE(crf.mr());

    crf.set_mr(true);
    EXPECT_TRUE(crf.mr());

    crf.set_mr(false);
    EXPECT_FALSE(crf.mr());
}

TEST(crfpdu_fields, fs_bit)
{
    CrfPdu crf;
    EXPECT_FALSE(crf.fs());

    crf.set_fs(true);
    EXPECT_TRUE(crf.fs());

    crf.set_fs(false);
    EXPECT_FALSE(crf.fs());
}

TEST(crfpdu_fields, tu_bit)
{
    CrfPdu crf;
    EXPECT_FALSE(crf.tu());

    crf.set_tu(true);
    EXPECT_TRUE(crf.tu());

    crf.set_tu(false);
    EXPECT_FALSE(crf.tu());
}

TEST(crfpdu_fields, sequence_num)
{
    CrfPdu crf;
    crf.set_sequence_num(42);
    EXPECT_EQ(crf.get_sequence_num(), 42);

    crf.set_sequence_num(255);
    EXPECT_EQ(crf.get_sequence_num(), 255);
}

TEST(crfpdu_fields, type)
{
    CrfPdu crf;
    crf.set_type(CRF_TYPE_AUDIO_SAMPLE);
    EXPECT_EQ(crf.get_type(), CRF_TYPE_AUDIO_SAMPLE);
    EXPECT_TRUE(crf.get_crf_type() == CrfType::audio_sample);

    crf.set_type(CrfType::video_frame);
    EXPECT_TRUE(crf.get_crf_type() == CrfType::video_frame);
}

TEST(crfpdu_fields, stream_id)
{
    CrfPdu crf;
    Eui48 system_addr(0x11, 0x22, 0x33, 0x44, 0x55, 0x66);
    StreamId sid(system_addr, 0xABCD);

    crf.set_stream_id(sid);
    StreamId result = crf.stream_id();

    EXPECT_TRUE(result.get_system_address() == system_addr);
    EXPECT_EQ(result.get_unique_id(), 0xABCD);
}

TEST(crfpdu_fields, pull)
{
    CrfPdu crf;
    crf.set_pull(CRF_PULL_MULT_1_DIV_1001);
    EXPECT_EQ(crf.pull(), CRF_PULL_MULT_1_DIV_1001);

    crf.set_pull(CrfPull::multiply_24_div_25);
    EXPECT_TRUE(crf.get_pull() == CrfPull::multiply_24_div_25);
}

TEST(crfpdu_fields, base_frequency)
{
    CrfPdu crf;
    crf.set_base_frequency(48000);
    EXPECT_EQ(crf.base_frequency(), 48000U);

    // Test maximum value (29 bits = 536870911)
    crf.set_base_frequency(0x1FFFFFFF);
    EXPECT_EQ(crf.base_frequency(), 0x1FFFFFFFU);

    // Test that pull field is preserved
    crf.set_pull(CRF_PULL_MULT_1001);
    crf.set_base_frequency(44100);
    EXPECT_EQ(crf.base_frequency(), 44100U);
    EXPECT_EQ(crf.pull(), CRF_PULL_MULT_1001);
}

TEST(crfpdu_fields, nominal_frequency)
{
    CrfPdu crf;
    crf.set_base_frequency(48000);
    crf.set_pull(CRF_PULL_MULT_1_0);
    EXPECT_TRUE(crf.nominal_frequency() == 48000.0);

    crf.set_pull(CRF_PULL_MULT_1_DIV_8);
    EXPECT_TRUE(crf.nominal_frequency() == 6000.0);
}

TEST(crfpdu_fields, crf_data_length)
{
    CrfPdu crf;
    crf.set_crf_data_length(48);  // 6 timestamps
    EXPECT_EQ(crf.crf_data_length(), 48);
    EXPECT_EQ(crf.timestamp_count(), 6);

    crf.set_crf_data_length(0);
    EXPECT_EQ(crf.crf_data_length(), 0);
    EXPECT_EQ(crf.timestamp_count(), 0);
}

TEST(crfpdu_fields, timestamp_interval)
{
    CrfPdu crf;
    crf.set_timestamp_interval(160);  // For 48kHz with 300 timestamps/sec
    EXPECT_EQ(crf.timestamp_interval(), 160);

    crf.set_timestamp_interval(1);
    EXPECT_EQ(crf.timestamp_interval(), 1);
}

//
// Tests: CrfPdu Type Check Helpers
//

TEST(crfpdu_type_check, is_user)
{
    CrfPdu crf;
    crf.set_type(CrfType::user);
    EXPECT_TRUE(crf.is_user_type());
    EXPECT_FALSE(crf.is_audio_sample_type());
}

TEST(crfpdu_type_check, is_audio_sample)
{
    CrfPdu crf;
    crf.set_type(CrfType::audio_sample);
    EXPECT_TRUE(crf.is_audio_sample_type());
    EXPECT_FALSE(crf.is_video_frame_type());
}

TEST(crfpdu_type_check, is_video_frame)
{
    CrfPdu crf;
    crf.set_type(CrfType::video_frame);
    EXPECT_TRUE(crf.is_video_frame_type());
    EXPECT_FALSE(crf.is_video_line_type());
}

TEST(crfpdu_type_check, is_video_line)
{
    CrfPdu crf;
    crf.set_type(CrfType::video_line);
    EXPECT_TRUE(crf.is_video_line_type());
    EXPECT_FALSE(crf.is_machine_cycle_type());
}

TEST(crfpdu_type_check, is_machine_cycle)
{
    CrfPdu crf;
    crf.set_type(CrfType::machine_cycle);
    EXPECT_TRUE(crf.is_machine_cycle_type());
    EXPECT_FALSE(crf.is_user_type());
}

//
// Tests: CrfPdu Initialization
//

TEST(crfpdu_init, audio_sample)
{
    CrfPdu crf;
    Eui48 system_addr(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
    StreamId sid(system_addr, 0x0001);

    crf.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160, 6);

    EXPECT_TRUE(crf.subtype == AvtpSubtype::crf);
    EXPECT_TRUE(crf.sv());
    EXPECT_FALSE(crf.mr());
    EXPECT_FALSE(crf.fs());
    EXPECT_FALSE(crf.tu());
    EXPECT_TRUE(crf.is_audio_sample_type());
    EXPECT_EQ(crf.base_frequency(), 48000U);
    EXPECT_TRUE(crf.get_pull() == CrfPull::multiply_1_0);
    EXPECT_EQ(crf.crf_data_length(), 48);  // 6 * 8 bytes
    EXPECT_EQ(crf.timestamp_interval(), 160);
}

TEST(crfpdu_init, video_frame)
{
    CrfPdu crf;
    Eui48 system_addr(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
    StreamId sid(system_addr, 0x0001);

    crf.init_video_frame(sid, 30, CrfPull::multiply_1_div_1001, 1, 1);

    EXPECT_TRUE(crf.subtype == AvtpSubtype::crf);
    EXPECT_TRUE(crf.is_video_frame_type());
    EXPECT_EQ(crf.base_frequency(), 30U);
    EXPECT_TRUE(crf.get_pull() == CrfPull::multiply_1_div_1001);
    EXPECT_EQ(crf.timestamp_interval(), 1);
}

TEST(crfpdu_init, video_line)
{
    CrfPdu crf;
    Eui48 system_addr(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
    StreamId sid(system_addr, 0x0001);

    crf.init_video_line(sid, 31500, CrfPull::multiply_1_0, 5, 5, true);

    EXPECT_TRUE(crf.is_video_line_type());
    EXPECT_TRUE(crf.fs());  // Frame sync should be set
    EXPECT_EQ(crf.base_frequency(), 31500U);
    EXPECT_EQ(crf.timestamp_interval(), 5);
}

TEST(crfpdu_init, machine_cycle)
{
    CrfPdu crf;
    Eui48 system_addr(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
    StreamId sid(system_addr, 0x0001);

    crf.init_machine_cycle(sid, 1000, CrfPull::multiply_1_0, 1, 2);

    EXPECT_TRUE(crf.is_machine_cycle_type());
    EXPECT_FALSE(crf.fs());  // Machine cycle should not have frame sync
    EXPECT_EQ(crf.base_frequency(), 1000U);
}

//
// Tests: CrfPdu Serialization
//

TEST(crfpdu_serial, roundtrip)
{
    CrfPdu crf;
    Eui48 system_addr(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
    StreamId sid(system_addr, 0xABCD);

    crf.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160, 6);
    crf.set_sequence_num(42);

    std::array<uint8_t, 20> buf{};
    auto stored = store_unchecked(buf, crf);
    EXPECT_EQ(stored, 20U);

    CrfPdu loaded;
    auto loaded_size = load_unchecked(buf, &loaded);
    EXPECT_EQ(loaded_size, 20U);

    EXPECT_TRUE(loaded.subtype == AvtpSubtype::crf);
    EXPECT_TRUE(loaded.is_audio_sample_type());
    EXPECT_EQ(loaded.base_frequency(), 48000U);
    EXPECT_EQ(loaded.get_sequence_num(), 42);
    EXPECT_EQ(loaded.timestamp_interval(), 160);
}

//
// Tests: CrfPdu Comparison
//

TEST(crfpdu_compare, equality)
{
    CrfPdu a, b;
    Eui48 system_addr(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
    StreamId sid(system_addr, 0x0001);

    a.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160, 6);
    b.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160, 6);

    EXPECT_TRUE(a == b);
}

TEST(crfpdu_compare, inequality)
{
    CrfPdu a, b;
    Eui48 system_addr(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
    StreamId sid(system_addr, 0x0001);

    a.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160, 6);
    b.init_audio_sample(sid, 44100, CrfPull::multiply_1_0, 147, 6);  // Different frequency

    EXPECT_TRUE(a != b);
}

//
// Tests: CRF Parsing Helpers
//

TEST(crf_parse, valid_header)
{
    CrfPdu crf;
    Eui48 system_addr(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
    StreamId sid(system_addr, 0x0001);
    crf.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160, 6);

    std::array<uint8_t, 20> buf{};
    (void)store_unchecked(buf, crf);

    auto parsed = crf_parse_header(buf);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->base_frequency(), 48000U);
}

TEST(crf_parse, too_small)
{
    std::array<uint8_t, 10> buf{};
    buf[0] = AvtpSubtype::crf;

    auto parsed = crf_parse_header(buf);
    EXPECT_FALSE(parsed.has_value());
}

TEST(crf_parse, wrong_subtype)
{
    std::array<uint8_t, 20> buf{};
    buf[0] = AvtpSubtype::aaf;  // Wrong subtype

    auto parsed = crf_parse_header(buf);
    EXPECT_FALSE(parsed.has_value());
}

//
// Tests: CRF Timestamp Helpers
//

TEST(crf_timestamps, get_timestamp)
{
    // Create timestamp data with known values (big-endian 64-bit)
    std::array<uint8_t, 24> ts_data{};  // 3 timestamps

    // Timestamp 0: 0x0000000100000000 (4294967296 ns)
    ts_data[0] = 0x00;
    ts_data[1] = 0x00;
    ts_data[2] = 0x00;
    ts_data[3] = 0x01;
    ts_data[4] = 0x00;
    ts_data[5] = 0x00;
    ts_data[6] = 0x00;
    ts_data[7] = 0x00;

    // Timestamp 1: 0x0000000100000001 (4294967297 ns)
    ts_data[8] = 0x00;
    ts_data[9] = 0x00;
    ts_data[10] = 0x00;
    ts_data[11] = 0x01;
    ts_data[12] = 0x00;
    ts_data[13] = 0x00;
    ts_data[14] = 0x00;
    ts_data[15] = 0x01;

    // Timestamp 2: 0xFFFFFFFFFFFFFFFF (max value)
    ts_data[16] = 0xFF;
    ts_data[17] = 0xFF;
    ts_data[18] = 0xFF;
    ts_data[19] = 0xFF;
    ts_data[20] = 0xFF;
    ts_data[21] = 0xFF;
    ts_data[22] = 0xFF;
    ts_data[23] = 0xFF;

    auto ts0 = crf_get_timestamp(ts_data, 0);
    auto ts1 = crf_get_timestamp(ts_data, 1);
    auto ts2 = crf_get_timestamp(ts_data, 2);

    EXPECT_TRUE(ts0.has_value());
    EXPECT_TRUE(ts1.has_value());
    EXPECT_TRUE(ts2.has_value());
    EXPECT_EQ(*ts0, 0x0000000100000000ULL);
    EXPECT_EQ(*ts1, 0x0000000100000001ULL);
    EXPECT_EQ(*ts2, 0xFFFFFFFFFFFFFFFFULL);
}

TEST(crf_timestamps, get_timestamp_zero)
{
    // Test that zero timestamp is distinguishable from error
    std::array<uint8_t, 8> ts_data{};  // All zeros - timestamp value is 0

    auto ts0 = crf_get_timestamp(ts_data, 0);
    EXPECT_TRUE(ts0.has_value());
    EXPECT_EQ(*ts0, 0ULL);
}

TEST(crf_timestamps, set_timestamp)
{
    std::array<uint8_t, 16> ts_data{};  // 2 timestamps

    bool success0 = crf_set_timestamp(ts_data, 0, 0x123456789ABCDEF0ULL);
    bool success1 = crf_set_timestamp(ts_data, 1, 0x0000000000000001ULL);

    EXPECT_TRUE(success0);
    EXPECT_TRUE(success1);
    EXPECT_EQ(*crf_get_timestamp(ts_data, 0), 0x123456789ABCDEF0ULL);
    EXPECT_EQ(*crf_get_timestamp(ts_data, 1), 0x0000000000000001ULL);
}

TEST(crf_timestamps, out_of_bounds_get)
{
    std::array<uint8_t, 8> ts_data{};  // 1 timestamp

    // Out of bounds read returns nullopt
    auto ts1 = crf_get_timestamp(ts_data, 1);
    auto ts100 = crf_get_timestamp(ts_data, 100);

    EXPECT_FALSE(ts1.has_value());
    EXPECT_FALSE(ts100.has_value());
}

TEST(crf_timestamps, out_of_bounds_set)
{
    std::array<uint8_t, 8> ts_data{};  // 1 timestamp

    // Out of bounds write returns false
    bool success = crf_set_timestamp(ts_data, 1, 12345ULL);
    EXPECT_FALSE(success);

    success = crf_set_timestamp(ts_data, 100, 12345ULL);
    EXPECT_FALSE(success);
}

//
// Tests: CrfPdu is_valid()
//

TEST(crfpdu_is_valid, valid_audio_sample)
{
    CrfPdu crf;
    Eui48 system_addr(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
    StreamId sid(system_addr, 0x0001);
    crf.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160, 6);

    EXPECT_TRUE(crf.is_valid());
}

TEST(crfpdu_is_valid, wrong_subtype)
{
    CrfPdu crf;
    Eui48 system_addr(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
    StreamId sid(system_addr, 0x0001);
    crf.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160, 6);

    // Corrupt the subtype
    crf.subtype = 0x02;  // AAF subtype

    EXPECT_FALSE(crf.is_valid());
}

TEST(crfpdu_is_valid, sv_not_set)
{
    CrfPdu crf;
    Eui48 system_addr(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
    StreamId sid(system_addr, 0x0001);
    crf.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160, 6);

    // Clear the sv bit
    crf.set_sv(false);

    EXPECT_FALSE(crf.is_valid());
}

TEST(crfpdu_is_valid, wrong_version)
{
    CrfPdu crf;
    Eui48 system_addr(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
    StreamId sid(system_addr, 0x0001);
    crf.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160, 6);

    // Set non-zero version
    crf.set_version(1);

    EXPECT_FALSE(crf.is_valid());
}

TEST(crfpdu_is_valid, invalid_type)
{
    CrfPdu crf;
    Eui48 system_addr(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
    StreamId sid(system_addr, 0x0001);
    crf.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160, 6);

    // Set invalid type (> 4)
    crf.set_type(0x05);

    EXPECT_FALSE(crf.is_valid());
}

TEST(crfpdu_is_valid, all_valid_types)
{
    CrfPdu crf;
    Eui48 system_addr(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
    StreamId sid(system_addr, 0x0001);

    // Test all valid types (0-4)
    crf.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160, 6);
    crf.set_type(CrfType::user);
    EXPECT_TRUE(crf.is_valid());

    crf.set_type(CrfType::audio_sample);
    EXPECT_TRUE(crf.is_valid());

    crf.set_type(CrfType::video_frame);
    EXPECT_TRUE(crf.is_valid());

    crf.set_type(CrfType::video_line);
    EXPECT_TRUE(crf.is_valid());

    crf.set_type(CrfType::machine_cycle);
    EXPECT_TRUE(crf.is_valid());
}

//
// Tests: CRF Timestamp Data Extraction
//

TEST(crf_timestamp_data, get_from_payload)
{
    // Build a complete CRF packet with header + timestamps
    CrfPdu crf;
    Eui48 system_addr(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
    StreamId sid(system_addr, 0x0001);
    crf.init_audio_sample(sid, 48000, CrfPull::multiply_1_0, 160, 2);

    std::array<uint8_t, 36> buf{};  // 20 header + 16 timestamp data
    std::span<uint8_t> buf_span{buf};
    (void)store_unchecked(buf_span.first(20), crf);

    // Add two timestamps
    (void)crf_set_timestamp(buf_span.subspan(20, 16), 0, 1000000ULL);
    (void)crf_set_timestamp(buf_span.subspan(20, 16), 1, 2000000ULL);

    auto ts_data = crf_get_timestamp_data(buf);
    EXPECT_EQ(ts_data.size(), 16U);
    EXPECT_EQ(*crf_get_timestamp(ts_data, 0), 1000000ULL);
    EXPECT_EQ(*crf_get_timestamp(ts_data, 1), 2000000ULL);
}

//
// Tests: Runtime coverage for constexpr utility functions
//

TEST(crf_rt_coverage, type_name_uint8_overload)
{
    uint8_t volatile t_user = CRF_TYPE_USER;
    uint8_t volatile t_audio = CRF_TYPE_AUDIO_SAMPLE;
    uint8_t volatile t_vframe = CRF_TYPE_VIDEO_FRAME;
    uint8_t volatile t_vline = CRF_TYPE_VIDEO_LINE;
    uint8_t volatile t_machine = CRF_TYPE_MACHINE_CYCLE;
    uint8_t volatile t_reserved = 0xFF;

    EXPECT_TRUE(std::string(crf_type_name(t_user)).starts_with("U"));
    EXPECT_TRUE(std::string(crf_type_name(t_audio)).starts_with("A"));
    EXPECT_TRUE(std::string(crf_type_name(t_vframe)).starts_with("V"));
    EXPECT_TRUE(std::string(crf_type_name(t_vline)).starts_with("V"));
    EXPECT_TRUE(std::string(crf_type_name(t_machine)).starts_with("M"));
    EXPECT_TRUE(std::string(crf_type_name(t_reserved)).starts_with("R"));
}

TEST(crf_rt_coverage, type_name_enum_overload)
{
    auto volatile t_user = CrfType::user;
    auto volatile t_audio = CrfType::audio_sample;
    auto volatile t_vframe = CrfType::video_frame;
    auto volatile t_vline = CrfType::video_line;
    auto volatile t_machine = CrfType::machine_cycle;

    EXPECT_TRUE(std::string(crf_type_name(t_user)).starts_with("U"));
    EXPECT_TRUE(std::string(crf_type_name(t_audio)).starts_with("A"));
    EXPECT_TRUE(std::string(crf_type_name(t_vframe)).starts_with("V"));
    EXPECT_TRUE(std::string(crf_type_name(t_vline)).starts_with("V"));
    EXPECT_TRUE(std::string(crf_type_name(t_machine)).starts_with("M"));
}

TEST(crf_rt_coverage, pull_name_uint8_overload)
{
    uint8_t volatile p_1_0 = CRF_PULL_MULT_1_0;
    uint8_t volatile p_1001 = CRF_PULL_MULT_1001;
    uint8_t volatile p_div1001 = CRF_PULL_MULT_1_DIV_1001;
    uint8_t volatile p_24_25 = CRF_PULL_MULT_24_DIV_25;
    uint8_t volatile p_25_24 = CRF_PULL_MULT_25_DIV_24;
    uint8_t volatile p_div8 = CRF_PULL_MULT_1_DIV_8;
    uint8_t volatile p_reserved = 0x07;

    EXPECT_TRUE(std::string(crf_pull_name(p_1_0)).size() > 0);
    EXPECT_TRUE(std::string(crf_pull_name(p_1001)).size() > 0);
    EXPECT_TRUE(std::string(crf_pull_name(p_div1001)).size() > 0);
    EXPECT_TRUE(std::string(crf_pull_name(p_24_25)).size() > 0);
    EXPECT_TRUE(std::string(crf_pull_name(p_25_24)).size() > 0);
    EXPECT_TRUE(std::string(crf_pull_name(p_div8)).size() > 0);
    EXPECT_TRUE(std::string(crf_pull_name(p_reserved)).starts_with("R"));
}

TEST(crf_rt_coverage, pull_name_enum_overload)
{
    auto volatile p_1_0 = CrfPull::multiply_1_0;
    auto volatile p_div1001 = CrfPull::multiply_1_div_1001;

    EXPECT_TRUE(std::string(crf_pull_name(p_1_0)).size() > 0);
    EXPECT_TRUE(std::string(crf_pull_name(p_div1001)).size() > 0);
}

TEST(crf_rt_coverage, calculate_frequency)
{
    uint32_t volatile base_48k = 48000;
    auto volatile pull_1_0 = CrfPull::multiply_1_0;
    uint8_t volatile pull_div8 = CRF_PULL_MULT_1_DIV_8;

    EXPECT_TRUE(crf_calculate_frequency(base_48k, pull_1_0) == 48000.0);
    EXPECT_TRUE(crf_calculate_frequency(base_48k, pull_div8) == 6000.0);
}

//
// Test Runner
//

TEST_MAIN(statusbar_avtp, avtp_crf_test)