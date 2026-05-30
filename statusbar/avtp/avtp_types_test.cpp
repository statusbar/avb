// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// AVTP Types Test - Tests for IEEE 1722-2016 AVTP type definitions
/// Tests comparison operators, conversion helpers, and classification predicates

#include "statusbar/avtp/avtp.hpp"
#include "statusbar/test/test.hpp"

#include <cstdint>

using namespace statusbar;
using namespace statusbar::avtp;

//
// Static Assert Tests - Compile-time verification of constexpr functions
//

// AVTP EtherType
static_assert(AVTP_ETHERTYPE == 0x22F0U, "AVTP EtherType must be 0x22F0");

// Constant values
static_assert(AvtpSubtype::aaf == 0x02U, "AvtpSubtype::aaf == 0x02");
static_assert(AvtpSubtype::crf == 0x04U, "AvtpSubtype::crf == 0x04");
static_assert(AvtpSubtype::maap == 0xFEU, "AvtpSubtype::maap == 0xFE");
static_assert(AvtpSubtype::iec_61883_iidc == 0x00U, "AvtpSubtype::iec_61883_iidc == 0x00");
static_assert(AvtpSubtype::ef_control == 0xFFU, "AvtpSubtype::ef_control == 0xFF");

// Header type name - tested at runtime (non-constexpr)

// Header type lookup (uint8_t)
static_assert(avtp_get_header_type(0x00U) == AvtpHeaderType::stream, "iec_61883_iidc is stream");
static_assert(avtp_get_header_type(0x02U) == AvtpHeaderType::stream, "aaf is stream");
static_assert(avtp_get_header_type(0x04U) == AvtpHeaderType::alternative, "crf is alternative");
static_assert(avtp_get_header_type(0x82U) == AvtpHeaderType::alternative, "ntscf is alternative");
static_assert(avtp_get_header_type(0xFAU) == AvtpHeaderType::control, "adp is control");
static_assert(avtp_get_header_type(0xFEU) == AvtpHeaderType::control, "maap is control");
static_assert(avtp_get_header_type(0x10U) == AvtpHeaderType::reserved, "0x10 is reserved");

// Header type lookup (namespace constant)
static_assert(avtp_get_header_type(AvtpSubtype::aaf) == AvtpHeaderType::stream, "aaf is stream");
static_assert(avtp_get_header_type(AvtpSubtype::crf) == AvtpHeaderType::alternative, "crf is alternative");
static_assert(avtp_get_header_type(AvtpSubtype::maap) == AvtpHeaderType::control, "maap is control");

// Encapsulation name - tested at runtime (non-constexpr)

// Encapsulation lookup (uint8_t)
static_assert(avtp_get_encapsulation(0x00U) == AvtpEncapsulation::continuous, "iec_61883_iidc is continuous");
static_assert(avtp_get_encapsulation(0x02U) == AvtpEncapsulation::continuous, "aaf is continuous");
static_assert(avtp_get_encapsulation(0x04U) == AvtpEncapsulation::continuous, "crf is continuous");
static_assert(avtp_get_encapsulation(0x82U) == AvtpEncapsulation::discrete, "ntscf is discrete");
static_assert(avtp_get_encapsulation(0xFAU) == AvtpEncapsulation::discrete, "adp is discrete");
static_assert(avtp_get_encapsulation(0xFEU) == AvtpEncapsulation::discrete, "maap is discrete");
static_assert(avtp_get_encapsulation(0x10U) == AvtpEncapsulation::reserved, "0x10 is reserved");

// Encapsulation lookup (namespace constant)
static_assert(avtp_get_encapsulation(AvtpSubtype::aaf) == AvtpEncapsulation::continuous, "aaf is continuous");
static_assert(avtp_get_encapsulation(AvtpSubtype::maap) == AvtpEncapsulation::discrete, "maap is discrete");

// Subtype name - tested at runtime (non-constexpr)

// Subtype description
static_assert(avtp_subtype_description(0x00U)[0] == 'I', "61883 description starts with I");
static_assert(avtp_subtype_description(0x02U)[0] == 'A', "AAF description starts with A");
static_assert(avtp_subtype_description(0x04U)[0] == 'C', "CRF description starts with C");
static_assert(avtp_subtype_description(0xFEU)[0] == 'M', "MAAP description starts with M");

// Subtype info - tested at runtime (avtp_get_subtype_info calls non-constexpr avtp_subtype_name)

// Classification predicates
static_assert(avtp_is_valid_subtype(0x00U), "iec_61883_iidc is valid");
static_assert(avtp_is_valid_subtype(0x02U), "aaf is valid");
static_assert(avtp_is_valid_subtype(0xFEU), "maap is valid");
static_assert(!avtp_is_valid_subtype(0x10U), "0x10 is not valid");
static_assert(!avtp_is_valid_subtype(0x50U), "0x50 is not valid");

static_assert(avtp_is_stream_subtype(0x00U), "iec_61883_iidc is stream");
static_assert(avtp_is_stream_subtype(0x02U), "aaf is stream");
static_assert(!avtp_is_stream_subtype(0x04U), "crf is not stream");
static_assert(!avtp_is_stream_subtype(0xFEU), "maap is not stream");

static_assert(avtp_is_control_subtype(0xFAU), "adp is control");
static_assert(avtp_is_control_subtype(0xFBU), "aecp is control");
static_assert(avtp_is_control_subtype(0xFCU), "acmp is control");
static_assert(avtp_is_control_subtype(0xFEU), "maap is control");
static_assert(!avtp_is_control_subtype(0x02U), "aaf is not control");

static_assert(avtp_is_alternative_subtype(0x04U), "crf is alternative");
static_assert(avtp_is_alternative_subtype(0x82U), "ntscf is alternative");
static_assert(!avtp_is_alternative_subtype(0x02U), "aaf is not alternative");
static_assert(!avtp_is_alternative_subtype(0xFEU), "maap is not alternative");

static_assert(avtp_is_continuous(0x00U), "iec_61883_iidc is continuous");
static_assert(avtp_is_continuous(0x02U), "aaf is continuous");
static_assert(avtp_is_continuous(0x04U), "crf is continuous");
static_assert(!avtp_is_continuous(0xFAU), "adp is not continuous");
static_assert(!avtp_is_continuous(0xFEU), "maap is not continuous");

static_assert(avtp_is_discrete(0xFAU), "adp is discrete");
static_assert(avtp_is_discrete(0xFBU), "aecp is discrete");
static_assert(avtp_is_discrete(0xFEU), "maap is discrete");
static_assert(!avtp_is_discrete(0x02U), "aaf is not discrete");
static_assert(!avtp_is_discrete(0x04U), "crf is not discrete");

static_assert(avtp_is_atdecc_subtype(0xFAU), "adp is atdecc");
static_assert(avtp_is_atdecc_subtype(0xFBU), "aecp is atdecc");
static_assert(avtp_is_atdecc_subtype(0xFCU), "acmp is atdecc");
static_assert(!avtp_is_atdecc_subtype(0xFEU), "maap is not atdecc");
static_assert(!avtp_is_atdecc_subtype(0x02U), "aaf is not atdecc");

static_assert(avtp_is_audio_subtype(0x00U), "iec_61883_iidc is audio");
static_assert(avtp_is_audio_subtype(0x02U), "aaf is audio");
static_assert(!avtp_is_audio_subtype(0x03U), "cvf is not audio");
static_assert(!avtp_is_audio_subtype(0x04U), "crf is not audio");

static_assert(avtp_is_video_subtype(0x03U), "cvf is video");
static_assert(avtp_is_video_subtype(0x06U), "svf is video");
static_assert(avtp_is_video_subtype(0x07U), "rvf is video");
static_assert(!avtp_is_video_subtype(0x02U), "aaf is not video");
static_assert(!avtp_is_video_subtype(0x04U), "crf is not video");

static_assert(avtp_is_encrypted_subtype(0x6EU), "aef_continuous is encrypted");
static_assert(avtp_is_encrypted_subtype(0xECU), "escf is encrypted");
static_assert(avtp_is_encrypted_subtype(0xEDU), "eecf is encrypted");
static_assert(avtp_is_encrypted_subtype(0xEEU), "aef_discrete is encrypted");
static_assert(!avtp_is_encrypted_subtype(0x02U), "aaf is not encrypted");

static_assert(avtp_is_experimental_subtype(0x7FU), "ef_stream is experimental");
static_assert(avtp_is_experimental_subtype(0xFFU), "ef_control is experimental");
static_assert(!avtp_is_experimental_subtype(0x02U), "aaf is not experimental");

// Helper to prevent constexpr folding - forces runtime evaluation
template <typename T>
[[gnu::noinline]] T runtime_value(T value)
{
    return value;
}

//
// Name function tests (runtime, non-constexpr)
//

TEST(avtp_type_names, header_type_name)
{
    EXPECT_EQ(avtp_header_type_name(AvtpHeaderType::stream)[0], 'S');
    EXPECT_EQ(avtp_header_type_name(AvtpHeaderType::control)[0], 'C');
    EXPECT_EQ(avtp_header_type_name(AvtpHeaderType::alternative)[0], 'A');
    EXPECT_EQ(avtp_header_type_name(AvtpHeaderType::reserved)[0], 'R');
}

TEST(avtp_type_names, encapsulation_name)
{
    EXPECT_EQ(avtp_encapsulation_name(AvtpEncapsulation::continuous)[0], 'C');
    EXPECT_EQ(avtp_encapsulation_name(AvtpEncapsulation::discrete)[0], 'D');
    EXPECT_EQ(avtp_encapsulation_name(AvtpEncapsulation::reserved)[0], 'R');
}

TEST(avtp_type_names, subtype_name_uint8)
{
    EXPECT_EQ(avtp_subtype_name(0x00U)[0], '6');
    EXPECT_EQ(avtp_subtype_name(0x02U)[0], 'A');
    EXPECT_EQ(avtp_subtype_name(0x04U)[0], 'C');
    EXPECT_EQ(avtp_subtype_name(0xFAU)[0], 'A');
    EXPECT_EQ(avtp_subtype_name(0xFEU)[0], 'M');
    EXPECT_EQ(avtp_subtype_name(0x10U)[0], 'R');
}

TEST(avtp_type_names, subtype_name_enum)
{
    EXPECT_EQ(avtp_subtype_name(AvtpSubtype::iec_61883_iidc)[0], '6');
    EXPECT_EQ(avtp_subtype_name(AvtpSubtype::aaf)[0], 'A');
    EXPECT_EQ(avtp_subtype_name(AvtpSubtype::crf)[0], 'C');
    EXPECT_EQ(avtp_subtype_name(AvtpSubtype::maap)[0], 'M');
}

//
// Constant Value Tests
//

TEST(avtp_subtype_constants, values)
{
    // Verify constants are uint8_t with correct values at runtime
    EXPECT_EQ(runtime_value(AvtpSubtype::iec_61883_iidc), 0x00U);
    EXPECT_EQ(runtime_value(AvtpSubtype::aaf), 0x02U);
    EXPECT_EQ(runtime_value(AvtpSubtype::crf), 0x04U);
    EXPECT_EQ(runtime_value(AvtpSubtype::maap), 0xFEU);
    EXPECT_EQ(runtime_value(AvtpSubtype::ef_control), 0xFFU);

    // Comparison with uint8_t works directly
    EXPECT_TRUE(runtime_value(AvtpSubtype::aaf) == runtime_value<uint8_t>(0x02U));
    EXPECT_TRUE(runtime_value(AvtpSubtype::crf) != runtime_value<uint8_t>(0x02U));
    EXPECT_TRUE(runtime_value<uint8_t>(0xFEU) == runtime_value(AvtpSubtype::maap));
    EXPECT_TRUE(runtime_value<uint8_t>(0x00U) != runtime_value(AvtpSubtype::aaf));
}

//
// Header Type Tests
//

TEST(avtp_header_type, name)
{
    EXPECT_EQ(std::string_view(avtp_header_type_name(runtime_value(AvtpHeaderType::stream))), "Stream");
    EXPECT_EQ(std::string_view(avtp_header_type_name(runtime_value(AvtpHeaderType::control))), "Control");
    EXPECT_EQ(std::string_view(avtp_header_type_name(runtime_value(AvtpHeaderType::alternative))), "Alternative");
    EXPECT_EQ(std::string_view(avtp_header_type_name(runtime_value(AvtpHeaderType::reserved))), "Reserved");
}

TEST(avtp_header_type, get_header_type_uint8)
{
    // Stream subtypes
    EXPECT_EQ(avtp_get_header_type(runtime_value<uint8_t>(0x00U)), AvtpHeaderType::stream);  // iec_61883_iidc
    EXPECT_EQ(avtp_get_header_type(runtime_value<uint8_t>(0x02U)), AvtpHeaderType::stream);  // aaf

    // Alternative subtypes
    EXPECT_EQ(avtp_get_header_type(runtime_value<uint8_t>(0x04U)), AvtpHeaderType::alternative);  // crf
    EXPECT_EQ(avtp_get_header_type(runtime_value<uint8_t>(0x82U)), AvtpHeaderType::alternative);  // ntscf

    // Control subtypes
    EXPECT_EQ(avtp_get_header_type(runtime_value<uint8_t>(0xFAU)), AvtpHeaderType::control);  // adp
    EXPECT_EQ(avtp_get_header_type(runtime_value<uint8_t>(0xFEU)), AvtpHeaderType::control);  // maap

    // Reserved
    EXPECT_EQ(avtp_get_header_type(runtime_value<uint8_t>(0x10U)), AvtpHeaderType::reserved);
}

TEST(avtp_header_type, get_header_type_constants)
{
    EXPECT_EQ(avtp_get_header_type(runtime_value(AvtpSubtype::aaf)), AvtpHeaderType::stream);
    EXPECT_EQ(avtp_get_header_type(runtime_value(AvtpSubtype::crf)), AvtpHeaderType::alternative);
    EXPECT_EQ(avtp_get_header_type(runtime_value(AvtpSubtype::maap)), AvtpHeaderType::control);
}

//
// Encapsulation Tests
//

TEST(avtp_encapsulation, name)
{
    EXPECT_EQ(std::string_view(avtp_encapsulation_name(runtime_value(AvtpEncapsulation::continuous))), "Continuous");
    EXPECT_EQ(std::string_view(avtp_encapsulation_name(runtime_value(AvtpEncapsulation::discrete))), "Discrete");
    EXPECT_EQ(std::string_view(avtp_encapsulation_name(runtime_value(AvtpEncapsulation::reserved))), "Reserved");
}

TEST(avtp_encapsulation, get_encapsulation_uint8)
{
    // Continuous
    EXPECT_EQ(avtp_get_encapsulation(runtime_value<uint8_t>(0x00U)), AvtpEncapsulation::continuous);  // iec_61883_iidc
    EXPECT_EQ(avtp_get_encapsulation(runtime_value<uint8_t>(0x02U)), AvtpEncapsulation::continuous);  // aaf
    EXPECT_EQ(avtp_get_encapsulation(runtime_value<uint8_t>(0x04U)), AvtpEncapsulation::continuous);  // crf

    // Discrete
    EXPECT_EQ(avtp_get_encapsulation(runtime_value<uint8_t>(0x82U)), AvtpEncapsulation::discrete);  // ntscf
    EXPECT_EQ(avtp_get_encapsulation(runtime_value<uint8_t>(0xFAU)), AvtpEncapsulation::discrete);  // adp
    EXPECT_EQ(avtp_get_encapsulation(runtime_value<uint8_t>(0xFEU)), AvtpEncapsulation::discrete);  // maap

    // Reserved
    EXPECT_EQ(avtp_get_encapsulation(runtime_value<uint8_t>(0x10U)), AvtpEncapsulation::reserved);
}

TEST(avtp_encapsulation, get_encapsulation_constants)
{
    EXPECT_EQ(avtp_get_encapsulation(runtime_value(AvtpSubtype::aaf)), AvtpEncapsulation::continuous);
    EXPECT_EQ(avtp_get_encapsulation(runtime_value(AvtpSubtype::maap)), AvtpEncapsulation::discrete);
}

//
// Subtype Name and Description Tests
//

TEST(avtp_subtype_names, name_uint8)
{
    EXPECT_EQ(std::string_view(avtp_subtype_name(runtime_value<uint8_t>(0x00U))), "61883_IIDC");
    EXPECT_EQ(std::string_view(avtp_subtype_name(runtime_value<uint8_t>(0x02U))), "AAF");
    EXPECT_EQ(std::string_view(avtp_subtype_name(runtime_value<uint8_t>(0x04U))), "CRF");
    EXPECT_EQ(std::string_view(avtp_subtype_name(runtime_value<uint8_t>(0xFAU))), "ADP");
    EXPECT_EQ(std::string_view(avtp_subtype_name(runtime_value<uint8_t>(0xFEU))), "MAAP");
    EXPECT_EQ(std::string_view(avtp_subtype_name(runtime_value<uint8_t>(0x10U))), "Reserved");
}

TEST(avtp_subtype_names, name_constant)
{
    EXPECT_EQ(std::string_view(avtp_subtype_name(runtime_value(AvtpSubtype::iec_61883_iidc))), "61883_IIDC");
    EXPECT_EQ(std::string_view(avtp_subtype_name(runtime_value(AvtpSubtype::aaf))), "AAF");
    EXPECT_EQ(std::string_view(avtp_subtype_name(runtime_value(AvtpSubtype::crf))), "CRF");
    EXPECT_EQ(std::string_view(avtp_subtype_name(runtime_value(AvtpSubtype::maap))), "MAAP");
}

TEST(avtp_subtype_names, description_uint8)
{
    EXPECT_EQ(std::string_view(avtp_subtype_description(runtime_value<uint8_t>(0x00U))), "IEC 61883/IIDC format");
    EXPECT_EQ(std::string_view(avtp_subtype_description(runtime_value<uint8_t>(0x02U))), "AVTP Audio Format");
    EXPECT_EQ(std::string_view(avtp_subtype_description(runtime_value<uint8_t>(0x04U))), "Clock Reference Format");
    EXPECT_EQ(std::string_view(avtp_subtype_description(runtime_value<uint8_t>(0xFEU))), "MAAP Protocol");
    EXPECT_EQ(std::string_view(avtp_subtype_description(runtime_value<uint8_t>(0x10U))), "Reserved");
}

//
// Subtype Info Tests
//

TEST(avtp_subtype_info, get_info_uint8)
{
    auto const info = avtp_get_subtype_info(runtime_value<uint8_t>(0x02U));
    EXPECT_EQ(info.subtype, AvtpSubtype::aaf);
    EXPECT_EQ(std::string_view(info.name), "AAF");
    EXPECT_EQ(std::string_view(info.description), "AVTP Audio Format");
    EXPECT_EQ(info.header_type, AvtpHeaderType::stream);
    EXPECT_EQ(info.encapsulation, AvtpEncapsulation::continuous);
}

TEST(avtp_subtype_info, get_info_constant)
{
    auto const info = avtp_get_subtype_info(runtime_value(AvtpSubtype::maap));
    EXPECT_EQ(info.subtype, AvtpSubtype::maap);
    EXPECT_EQ(std::string_view(info.name), "MAAP");
    EXPECT_EQ(info.header_type, AvtpHeaderType::control);
    EXPECT_EQ(info.encapsulation, AvtpEncapsulation::discrete);
}

//
// Classification Predicate Tests
//

TEST(avtp_predicates, is_valid_subtype)
{
    EXPECT_TRUE(avtp_is_valid_subtype(runtime_value<uint8_t>(0x00U)));   // iec_61883_iidc
    EXPECT_TRUE(avtp_is_valid_subtype(runtime_value<uint8_t>(0x02U)));   // aaf
    EXPECT_TRUE(avtp_is_valid_subtype(runtime_value<uint8_t>(0xFEU)));   // maap
    EXPECT_FALSE(avtp_is_valid_subtype(runtime_value<uint8_t>(0x10U)));  // reserved
    EXPECT_FALSE(avtp_is_valid_subtype(runtime_value<uint8_t>(0x50U)));  // reserved
}

TEST(avtp_predicates, is_stream_subtype)
{
    EXPECT_TRUE(avtp_is_stream_subtype(runtime_value<uint8_t>(0x00U)));   // iec_61883_iidc
    EXPECT_TRUE(avtp_is_stream_subtype(runtime_value<uint8_t>(0x02U)));   // aaf
    EXPECT_FALSE(avtp_is_stream_subtype(runtime_value<uint8_t>(0x04U)));  // crf (alternative)
    EXPECT_FALSE(avtp_is_stream_subtype(runtime_value<uint8_t>(0xFEU)));  // maap (control)
}

TEST(avtp_predicates, is_control_subtype)
{
    EXPECT_TRUE(avtp_is_control_subtype(runtime_value<uint8_t>(0xFAU)));   // adp
    EXPECT_TRUE(avtp_is_control_subtype(runtime_value<uint8_t>(0xFBU)));   // aecp
    EXPECT_TRUE(avtp_is_control_subtype(runtime_value<uint8_t>(0xFCU)));   // acmp
    EXPECT_TRUE(avtp_is_control_subtype(runtime_value<uint8_t>(0xFEU)));   // maap
    EXPECT_FALSE(avtp_is_control_subtype(runtime_value<uint8_t>(0x02U)));  // aaf (stream)
}

TEST(avtp_predicates, is_alternative_subtype)
{
    EXPECT_TRUE(avtp_is_alternative_subtype(runtime_value<uint8_t>(0x04U)));   // crf
    EXPECT_TRUE(avtp_is_alternative_subtype(runtime_value<uint8_t>(0x82U)));   // ntscf
    EXPECT_FALSE(avtp_is_alternative_subtype(runtime_value<uint8_t>(0x02U)));  // aaf (stream)
    EXPECT_FALSE(avtp_is_alternative_subtype(runtime_value<uint8_t>(0xFEU)));  // maap (control)
}

TEST(avtp_predicates, is_continuous)
{
    EXPECT_TRUE(avtp_is_continuous(runtime_value<uint8_t>(0x00U)));   // iec_61883_iidc
    EXPECT_TRUE(avtp_is_continuous(runtime_value<uint8_t>(0x02U)));   // aaf
    EXPECT_TRUE(avtp_is_continuous(runtime_value<uint8_t>(0x04U)));   // crf
    EXPECT_FALSE(avtp_is_continuous(runtime_value<uint8_t>(0xFAU)));  // adp (discrete)
    EXPECT_FALSE(avtp_is_continuous(runtime_value<uint8_t>(0xFEU)));  // maap (discrete)
}

TEST(avtp_predicates, is_discrete)
{
    EXPECT_TRUE(avtp_is_discrete(runtime_value<uint8_t>(0xFAU)));   // adp
    EXPECT_TRUE(avtp_is_discrete(runtime_value<uint8_t>(0xFBU)));   // aecp
    EXPECT_TRUE(avtp_is_discrete(runtime_value<uint8_t>(0xFEU)));   // maap
    EXPECT_FALSE(avtp_is_discrete(runtime_value<uint8_t>(0x02U)));  // aaf (continuous)
    EXPECT_FALSE(avtp_is_discrete(runtime_value<uint8_t>(0x04U)));  // crf (continuous)
}

TEST(avtp_predicates, is_atdecc_subtype)
{
    EXPECT_TRUE(avtp_is_atdecc_subtype(runtime_value<uint8_t>(0xFAU)));   // adp
    EXPECT_TRUE(avtp_is_atdecc_subtype(runtime_value<uint8_t>(0xFBU)));   // aecp
    EXPECT_TRUE(avtp_is_atdecc_subtype(runtime_value<uint8_t>(0xFCU)));   // acmp
    EXPECT_FALSE(avtp_is_atdecc_subtype(runtime_value<uint8_t>(0xFEU)));  // maap (not ATDECC)
    EXPECT_FALSE(avtp_is_atdecc_subtype(runtime_value<uint8_t>(0x02U)));  // aaf (not ATDECC)
}

TEST(avtp_predicates, is_audio_subtype)
{
    EXPECT_TRUE(avtp_is_audio_subtype(runtime_value<uint8_t>(0x00U)));   // iec_61883_iidc
    EXPECT_TRUE(avtp_is_audio_subtype(runtime_value<uint8_t>(0x02U)));   // aaf
    EXPECT_FALSE(avtp_is_audio_subtype(runtime_value<uint8_t>(0x03U)));  // cvf (video)
    EXPECT_FALSE(avtp_is_audio_subtype(runtime_value<uint8_t>(0x04U)));  // crf (not audio)
}

TEST(avtp_predicates, is_video_subtype)
{
    EXPECT_TRUE(avtp_is_video_subtype(runtime_value<uint8_t>(0x03U)));   // cvf
    EXPECT_TRUE(avtp_is_video_subtype(runtime_value<uint8_t>(0x06U)));   // svf
    EXPECT_TRUE(avtp_is_video_subtype(runtime_value<uint8_t>(0x07U)));   // rvf
    EXPECT_FALSE(avtp_is_video_subtype(runtime_value<uint8_t>(0x02U)));  // aaf (audio)
    EXPECT_FALSE(avtp_is_video_subtype(runtime_value<uint8_t>(0x04U)));  // crf (not video)
}

TEST(avtp_predicates, is_encrypted_subtype)
{
    EXPECT_TRUE(avtp_is_encrypted_subtype(runtime_value<uint8_t>(0x6EU)));   // aef_continuous
    EXPECT_TRUE(avtp_is_encrypted_subtype(runtime_value<uint8_t>(0xECU)));   // escf
    EXPECT_TRUE(avtp_is_encrypted_subtype(runtime_value<uint8_t>(0xEDU)));   // eecf
    EXPECT_TRUE(avtp_is_encrypted_subtype(runtime_value<uint8_t>(0xEEU)));   // aef_discrete
    EXPECT_FALSE(avtp_is_encrypted_subtype(runtime_value<uint8_t>(0x02U)));  // aaf (not encrypted)
}

TEST(avtp_predicates, is_experimental_subtype)
{
    EXPECT_TRUE(avtp_is_experimental_subtype(runtime_value<uint8_t>(0x7FU)));   // ef_stream
    EXPECT_TRUE(avtp_is_experimental_subtype(runtime_value<uint8_t>(0xFFU)));   // ef_control
    EXPECT_FALSE(avtp_is_experimental_subtype(runtime_value<uint8_t>(0x02U)));  // aaf (not experimental)
}

//
// AVTP EtherType Test
//

TEST(avtp_ethertype, value)
{
    EXPECT_EQ(AVTP_ETHERTYPE, 0x22F0U);
}

//
// Test Runner
//

TEST_MAIN(statusbar_avtp, avtp_types_test)