// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for ATDECC Discovery Protocol (ADP) data units

#include "statusbar/atdecc/atdecc_format.hpp"
#include "statusbar/avtp/avtp.hpp"
#include "statusbar/buffer/buffer.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <cstdint>
#include <string>

using namespace statusbar;
using namespace statusbar::atdecc;
using namespace statusbar::avtp;
using namespace statusbar::ieee;

//
// AdpDu construction and defaults
//

TEST(adp_pdu, construction_defaults)
{
    AdpDu adp{};
    EXPECT_EQ(adp.subtype.get(), 0u);
    EXPECT_EQ(adp.sv(), false);
    EXPECT_EQ(adp.version(), 0u);
    EXPECT_EQ(adp.message_type(), 0u);
    EXPECT_EQ(adp.valid_time(), 0u);
    EXPECT_EQ(adp.control_data_length(), 0u);
    EXPECT_EQ(adp.entity_id, Eui64{});
    EXPECT_EQ(adp.entity_model_id, Eui64{});
    EXPECT_EQ(adp.entity_capabilities.get(), 0u);
    EXPECT_EQ(adp.talker_stream_sources.get(), 0u);
    EXPECT_EQ(adp.talker_capabilities.get(), 0u);
    EXPECT_EQ(adp.listener_stream_sinks.get(), 0u);
    EXPECT_EQ(adp.listener_capabilities.get(), 0u);
    EXPECT_EQ(adp.controller_capabilities.get(), 0u);
    EXPECT_EQ(adp.available_index.get(), 0u);
    EXPECT_EQ(adp.gptp_domain_number.get(), 0u);
    EXPECT_EQ(adp.identify_control_index.get(), 0u);
    EXPECT_EQ(adp.interface_index.get(), 0u);
    EXPECT_EQ(adp.association_id, Eui64{});
}

//
// Entity ID accessors
//

TEST(adp_pdu, entity_id_accessors)
{
    AdpDu adp{};
    Eui64 const test_id(0x00, 0x1b, 0x21, 0xff, 0xfe, 0x00, 0x01, 0x02);
    adp.entity_id = test_id;
    EXPECT_EQ(adp.entity_id, test_id);

    Eui64 const model_id(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11);
    adp.entity_model_id = model_id;
    EXPECT_EQ(adp.entity_model_id, model_id);
}

//
// Valid time field packing
//

TEST(adp_pdu, valid_time_encoding)
{
    AdpDu adp{};
    adp.set_valid_time(0);
    EXPECT_EQ(adp.valid_time(), 0u);

    adp.set_valid_time(31);
    EXPECT_EQ(adp.valid_time(), 31u);

    adp.set_valid_time(15);
    EXPECT_EQ(adp.valid_time(), 15u);

    // valid_time is in 2-second units, so 31 = 62 seconds
    adp.set_valid_time(31);
    EXPECT_EQ(adp.valid_time() * 2, 62u);

    // Verify valid_time doesn't clobber control_data_length low bits
    adp.set_control_data_length(56);
    adp.set_valid_time(10);
    EXPECT_EQ(adp.valid_time(), 10u);
    EXPECT_EQ(adp.control_data_length(), 56u);
}

//
// Capability flags
//

TEST(adp_pdu, capability_flags)
{
    AdpDu adp{};

    // Entity capabilities
    adp.entity_capabilities = entity_capabilities::AEM_SUPPORTED | entity_capabilities::CLASS_A_SUPPORTED;
    EXPECT_TRUE(adp.has_entity_capability(entity_capabilities::AEM_SUPPORTED));
    EXPECT_TRUE(adp.has_entity_capability(entity_capabilities::CLASS_A_SUPPORTED));
    EXPECT_FALSE(adp.has_entity_capability(entity_capabilities::GPTP_SUPPORTED));
    EXPECT_FALSE(adp.has_entity_capability(entity_capabilities::EFU_MODE));

    // Talker capabilities
    adp.talker_capabilities = talker_capabilities::IMPLEMENTED | talker_capabilities::AUDIO_SOURCE;
    EXPECT_TRUE(adp.has_talker_capability(talker_capabilities::IMPLEMENTED));
    EXPECT_TRUE(adp.has_talker_capability(talker_capabilities::AUDIO_SOURCE));
    EXPECT_FALSE(adp.has_talker_capability(talker_capabilities::VIDEO_SOURCE));

    // Listener capabilities
    adp.listener_capabilities = listener_capabilities::IMPLEMENTED | listener_capabilities::AUDIO_SINK;
    EXPECT_TRUE(adp.has_listener_capability(listener_capabilities::IMPLEMENTED));
    EXPECT_TRUE(adp.has_listener_capability(listener_capabilities::AUDIO_SINK));
    EXPECT_FALSE(adp.has_listener_capability(listener_capabilities::VIDEO_SINK));

    // Controller capabilities
    adp.controller_capabilities = controller_capabilities::IMPLEMENTED;
    EXPECT_TRUE(adp.has_controller_capability(controller_capabilities::IMPLEMENTED));
}

//
// Initialization functions
//

TEST(adp_pdu, init_entity_available)
{
    AdpDu adp{};
    Eui64 const eid(0x00, 0x1b, 0x21, 0xff, 0xfe, 0x00, 0x01, 0x02);
    adp.init_entity_available(eid, 31);

    EXPECT_EQ(adp.subtype.get(), AvtpSubtype::adp);
    EXPECT_TRUE(adp.is_entity_available());
    EXPECT_FALSE(adp.is_entity_departing());
    EXPECT_FALSE(adp.is_entity_discover());
    EXPECT_EQ(adp.valid_time(), 31u);
    EXPECT_EQ(adp.control_data_length(), AdpDu::DATA_LENGTH);
    EXPECT_EQ(adp.entity_id, eid);
    EXPECT_TRUE(adp.is_valid());
}

TEST(adp_pdu, init_entity_available_default_valid_time)
{
    AdpDu adp{};
    Eui64 const eid(0x00, 0x1b, 0x21, 0xff, 0xfe, 0x00, 0x01, 0x02);
    adp.init_entity_available(eid);
    EXPECT_EQ(adp.valid_time(), 31u);  // default
}

TEST(adp_pdu, init_entity_departing)
{
    AdpDu adp{};
    Eui64 const eid(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11);
    adp.init_entity_departing(eid);

    EXPECT_EQ(adp.subtype.get(), AvtpSubtype::adp);
    EXPECT_FALSE(adp.is_entity_available());
    EXPECT_TRUE(adp.is_entity_departing());
    EXPECT_FALSE(adp.is_entity_discover());
    EXPECT_EQ(adp.valid_time(), 0u);
    EXPECT_EQ(adp.control_data_length(), AdpDu::DATA_LENGTH);
    EXPECT_EQ(adp.entity_id, eid);
    EXPECT_TRUE(adp.is_valid());
}

TEST(adp_pdu, init_entity_discover)
{
    AdpDu adp{};
    adp.init_entity_discover();

    EXPECT_EQ(adp.subtype.get(), AvtpSubtype::adp);
    EXPECT_FALSE(adp.is_entity_available());
    EXPECT_FALSE(adp.is_entity_departing());
    EXPECT_TRUE(adp.is_entity_discover());
    EXPECT_EQ(adp.valid_time(), 0u);
    EXPECT_EQ(adp.control_data_length(), AdpDu::DATA_LENGTH);
    EXPECT_EQ(adp.entity_id, Eui64{});
    EXPECT_TRUE(adp.is_valid());
}

//
// Validation
//

TEST(adp_pdu, validation)
{
    // Default-constructed is not valid (subtype is 0, not ADP)
    AdpDu adp{};
    EXPECT_FALSE(adp.is_valid());

    // After init, should be valid
    adp.init_entity_available(Eui64{}, 10);
    EXPECT_TRUE(adp.is_valid());

    // Bad subtype
    AdpDu bad = adp;
    bad.subtype = 0x00;
    EXPECT_FALSE(bad.is_valid());

    // Control data length too short
    AdpDu short_cdl = adp;
    short_cdl.set_control_data_length(10);
    EXPECT_FALSE(short_cdl.is_valid());

    // Invalid message type
    AdpDu bad_mt = adp;
    bad_mt.set_message_type(5);
    EXPECT_FALSE(bad_mt.is_valid());
}

//
// Serialization roundtrip via memcpy (wire format struct)
//

TEST(adp_pdu, serialization_roundtrip)
{
    AdpDu original{};
    Eui64 const eid(0x00, 0x1b, 0x21, 0xff, 0xfe, 0x00, 0x01, 0x02);
    original.init_entity_available(eid, 20);
    original.entity_model_id = Eui64(0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88);
    original.entity_capabilities = entity_capabilities::AEM_SUPPORTED | entity_capabilities::GPTP_SUPPORTED;
    original.talker_stream_sources = 2;
    original.talker_capabilities = talker_capabilities::IMPLEMENTED | talker_capabilities::AUDIO_SOURCE;
    original.listener_stream_sinks = 4;
    original.listener_capabilities = listener_capabilities::IMPLEMENTED | listener_capabilities::AUDIO_SINK;
    original.available_index = 42;

    // Serialize to raw bytes
    std::array<uint8_t, AdpDu::LENGTH> wire{};
    span_store(wire, original);

    // Deserialize from raw bytes
    AdpDu restored{};
    span_load(restored, wire);

    EXPECT_EQ(original, restored);
    EXPECT_TRUE(restored.is_valid());
    EXPECT_EQ(restored.entity_id, eid);
    EXPECT_EQ(restored.valid_time(), 20u);
    EXPECT_EQ(restored.entity_model_id, original.entity_model_id);
    EXPECT_EQ(restored.entity_capabilities.get(), original.entity_capabilities.get());
    EXPECT_EQ(restored.talker_stream_sources.get(), 2u);
    EXPECT_EQ(restored.listener_stream_sinks.get(), 4u);
    EXPECT_EQ(restored.available_index.get(), 42u);
}

//
// Wire format layout (compile-time verified in header, runtime sanity check)
//

TEST(adp_pdu, wire_format_layout)
{
    EXPECT_EQ(sizeof(AdpDu), 68u);
    EXPECT_EQ(AdpDu::LENGTH, 68u);
    EXPECT_EQ(AdpDu::HEADER_LENGTH, 12u);
    EXPECT_EQ(AdpDu::DATA_LENGTH, 56u);
    EXPECT_EQ(offsetof(AdpDu, subtype), 0u);
    EXPECT_EQ(offsetof(AdpDu, entity_id), 4u);
    EXPECT_EQ(offsetof(AdpDu, entity_model_id), 12u);
    EXPECT_EQ(offsetof(AdpDu, entity_capabilities), 20u);
    EXPECT_EQ(offsetof(AdpDu, talker_stream_sources), 24u);
    EXPECT_EQ(offsetof(AdpDu, talker_capabilities), 26u);
    EXPECT_EQ(offsetof(AdpDu, listener_stream_sinks), 28u);
    EXPECT_EQ(offsetof(AdpDu, listener_capabilities), 30u);
    EXPECT_EQ(offsetof(AdpDu, controller_capabilities), 32u);
    EXPECT_EQ(offsetof(AdpDu, available_index), 36u);
    EXPECT_EQ(offsetof(AdpDu, gptp_grandmaster_id), 40u);
    EXPECT_EQ(offsetof(AdpDu, gptp_domain_number), 48u);
    EXPECT_EQ(offsetof(AdpDu, association_id), 56u);
    EXPECT_EQ(offsetof(AdpDu, reserved1), 64u);
}

//
// Message type name lookup
//

TEST(adp_pdu, message_type_names)
{
    EXPECT_TRUE(std::string(adp_message_type_name(ADP_MESSAGE_TYPE_ENTITY_AVAILABLE)) == "Entity Available");
    EXPECT_TRUE(std::string(adp_message_type_name(ADP_MESSAGE_TYPE_ENTITY_DEPARTING)) == "Entity Departing");
    EXPECT_TRUE(std::string(adp_message_type_name(ADP_MESSAGE_TYPE_ENTITY_DISCOVER)) == "Entity Discover");
    EXPECT_TRUE(std::string(adp_message_type_name(0xFF)) == "Unknown");
}

//
// Capability formatting
//

TEST(adp_pdu, format_entity_capabilities)
{
    std::string result;
    format_entity_capabilities_to(std::back_inserter(result), entity_capabilities::AEM_SUPPORTED);
    EXPECT_TRUE(result.find("AEM") != std::string::npos);

    result.clear();
    format_entity_capabilities_to(
        std::back_inserter(result), entity_capabilities::AEM_SUPPORTED | entity_capabilities::GPTP_SUPPORTED);
    EXPECT_TRUE(result.find("AEM") != std::string::npos);
    EXPECT_TRUE(result.find("GPTP") != std::string::npos);
    EXPECT_TRUE(result.find(",") != std::string::npos);

    result.clear();
    format_entity_capabilities_to(std::back_inserter(result), 0);
    EXPECT_TRUE(result == "(none)");
}

TEST(adp_pdu, format_entity_capabilities_new_flags)
{
    // Test each new flag individually
    auto check_flag = [](uint32_t flag, std::string_view expected_substr) {
        std::string result;
        format_entity_capabilities_to(std::back_inserter(result), flag);
        EXPECT_TRUE(result.find(expected_substr) != std::string::npos);
    };

    check_flag(entity_capabilities::ACMP_ACQUIRE_WITH_AEM, "ACMP_ACQUIRE");
    check_flag(entity_capabilities::ACMP_AUTHENTICATE_WITH_AEM, "ACMP_AUTH");
    check_flag(entity_capabilities::SUPPORTS_UDPV4_ATDECC, "UDPv4_ATDECC");
    check_flag(entity_capabilities::SUPPORTS_UDPV4_STREAMING, "UDPv4_STREAM");
    check_flag(entity_capabilities::SUPPORTS_UDPV6_ATDECC, "UDPv6_ATDECC");
    check_flag(entity_capabilities::SUPPORTS_UDPV6_STREAMING, "UDPv6_STREAM");
    check_flag(entity_capabilities::MULTIPLE_PTP_INSTANCES, "MULTI_PTP");
    check_flag(entity_capabilities::AEM_CONFIGURATION_INDEX_VALID, "CONFIG_IDX_VALID");

    // Test combined old + new flags
    std::string result;
    format_entity_capabilities_to(
        std::back_inserter(result),
        entity_capabilities::AEM_SUPPORTED | entity_capabilities::ACMP_ACQUIRE_WITH_AEM |
            entity_capabilities::AEM_CONFIGURATION_INDEX_VALID);
    EXPECT_TRUE(result.find("AEM") != std::string::npos);
    EXPECT_TRUE(result.find("ACMP_ACQUIRE") != std::string::npos);
    EXPECT_TRUE(result.find("CONFIG_IDX_VALID") != std::string::npos);
}

TEST(adp_pdu, format_talker_capabilities)
{
    std::string result;
    format_talker_capabilities_to(std::back_inserter(result), talker_capabilities::IMPLEMENTED | talker_capabilities::AUDIO_SOURCE);
    EXPECT_TRUE(result.find("IMPLEMENTED") != std::string::npos);
    EXPECT_TRUE(result.find("AUDIO") != std::string::npos);

    result.clear();
    format_talker_capabilities_to(std::back_inserter(result), 0);
    EXPECT_TRUE(result == "(none)");
}

TEST(adp_pdu, format_listener_capabilities)
{
    std::string result;
    format_listener_capabilities_to(
        std::back_inserter(result), listener_capabilities::IMPLEMENTED | listener_capabilities::AUDIO_SINK);
    EXPECT_TRUE(result.find("IMPLEMENTED") != std::string::npos);
    EXPECT_TRUE(result.find("AUDIO") != std::string::npos);

    result.clear();
    format_listener_capabilities_to(std::back_inserter(result), 0);
    EXPECT_TRUE(result == "(none)");
}

//
// AdpDu format_to
//

TEST(adp_pdu, format_to_entity_available)
{
    AdpDu adp{};
    Eui64 const eid(0x00, 0x1b, 0x21, 0xff, 0xfe, 0x00, 0x01, 0x02);
    adp.init_entity_available(eid, 10);
    adp.entity_capabilities = entity_capabilities::AEM_SUPPORTED;

    std::string result;
    format_to(std::back_inserter(result), adp);
    EXPECT_TRUE(result.find("ADP:") != std::string::npos);
    EXPECT_TRUE(result.find("Entity Available") != std::string::npos);
    EXPECT_TRUE(result.find("valid_time=20s") != std::string::npos);
}

TEST(adp_pdu, format_to_entity_departing)
{
    AdpDu adp{};
    Eui64 const eid(0x00, 0x1b, 0x21, 0xff, 0xfe, 0x00, 0x01, 0x02);
    adp.init_entity_departing(eid);

    std::string result;
    format_to(std::back_inserter(result), adp);
    EXPECT_TRUE(result.find("Entity Departing") != std::string::npos);
}

//
// Control data length field
//

TEST(adp_pdu, control_data_length)
{
    AdpDu adp{};
    adp.set_control_data_length(56);
    EXPECT_EQ(adp.control_data_length(), 56u);

    adp.set_control_data_length(0);
    EXPECT_EQ(adp.control_data_length(), 0u);

    // 11-bit max = 2047
    adp.set_control_data_length(2047);
    EXPECT_EQ(adp.control_data_length(), 2047u);
}

//
// Message type set/get
//

TEST(adp_pdu, message_type_set_get)
{
    AdpDu adp{};
    adp.set_message_type(ADP_MESSAGE_TYPE_ENTITY_AVAILABLE);
    EXPECT_EQ(adp.message_type(), ADP_MESSAGE_TYPE_ENTITY_AVAILABLE);

    adp.set_message_type(ADP_MESSAGE_TYPE_ENTITY_DEPARTING);
    EXPECT_EQ(adp.message_type(), ADP_MESSAGE_TYPE_ENTITY_DEPARTING);

    adp.set_message_type(ADP_MESSAGE_TYPE_ENTITY_DISCOVER);
    EXPECT_EQ(adp.message_type(), ADP_MESSAGE_TYPE_ENTITY_DISCOVER);
}

TEST(adp_pdu, current_configuration_index)
{
    AdpDu adp{};
    adp.current_configuration_index = doublet_t{42};
    EXPECT_EQ(adp.current_configuration_index.get(), 42U);

    // Verify wire format: network byte order at offsets 50-51
    auto const raw = make_const_span(adp);
    EXPECT_EQ(raw[50], 0x00);
    EXPECT_EQ(raw[51], 0x2A);  // 42 in big-endian

    // Round-trip: copy bytes and read back via span_load.
    AdpDu adp2{};
    span_load(adp2, make_const_span(adp));
    EXPECT_EQ(adp2.current_configuration_index.get(), 42U);
}

//
// Test Runner
//

TEST_MAIN(statusbar_atdecc, atdecc_adp_test)
