// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/atdecc/atdecc_aecp_aa.hpp"

#include "statusbar/buffer/span_utils.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <vector>

using namespace statusbar::atdecc;
using statusbar::make_const_span;
using statusbar::span_load;
using statusbar::ieee::Eui64;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

TEST(aecp_aa, mode_names)
{
    EXPECT_EQ(std::string_view{aa_mode_name(AA_MODE_READ)}, "READ");
    EXPECT_EQ(std::string_view{aa_mode_name(AA_MODE_WRITE)}, "WRITE");
    EXPECT_EQ(std::string_view{aa_mode_name(AA_MODE_EXECUTE)}, "EXECUTE");
    EXPECT_EQ(std::string_view{aa_mode_name(15)}, "Unknown");
}

TEST(aecp_aa, status_names)
{
    EXPECT_EQ(std::string_view{aa_status_name(AA_STATUS_SUCCESS)}, "SUCCESS");
    EXPECT_EQ(std::string_view{aa_status_name(AA_STATUS_NOT_IMPLEMENTED)}, "NOT_IMPLEMENTED");
    EXPECT_EQ(std::string_view{aa_status_name(AA_STATUS_ADDRESS_TOO_LOW)}, "ADDRESS_TOO_LOW");
    EXPECT_EQ(std::string_view{aa_status_name(AA_STATUS_ADDRESS_TOO_HIGH)}, "ADDRESS_TOO_HIGH");
    EXPECT_EQ(std::string_view{aa_status_name(AA_STATUS_ADDRESS_INVALID)}, "ADDRESS_INVALID");
    EXPECT_EQ(std::string_view{aa_status_name(AA_STATUS_TLV_INVALID)}, "TLV_INVALID");
    EXPECT_EQ(std::string_view{aa_status_name(AA_STATUS_DATA_INVALID)}, "DATA_INVALID");
    EXPECT_EQ(std::string_view{aa_status_name(AA_STATUS_UNSUPPORTED)}, "UNSUPPORTED");
    EXPECT_EQ(std::string_view{aa_status_name(31)}, "Unknown");
}

// ---------------------------------------------------------------------------
// PDU header
// ---------------------------------------------------------------------------

TEST(aecp_aa, pdu_size)
{
    EXPECT_EQ(sizeof(AecpAaDu), 24u);
}

TEST(aecp_aa, init_command)
{
    Eui64 const target{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    Eui64 const controller{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x01};

    AecpAaDu pdu{};
    pdu.init_command(target, controller, 42, 2, 30);

    EXPECT_TRUE(pdu.is_valid());
    EXPECT_TRUE(pdu.common.is_command());
    EXPECT_TRUE(pdu.common.is_address_access());
    EXPECT_EQ(pdu.common.message_type(), AECP_MESSAGE_TYPE_ADDRESS_ACCESS_COMMAND);
    EXPECT_EQ(pdu.common.status(), AECP_STATUS_SUCCESS);
    EXPECT_EQ(pdu.common.sequence_id.get(), 42u);
    EXPECT_EQ(pdu.tlv_count.get(), 2u);
    // control_data_length = COMMON_DATA_LENGTH(10) + 2(tlv_count) + 30(payload)
    EXPECT_EQ(pdu.common.control_data_length(), 42u);
}

TEST(aecp_aa, init_response)
{
    Eui64 const target{};
    Eui64 const controller{};

    AecpAaDu pdu{};
    pdu.init_response(target, controller, 7, AA_STATUS_ADDRESS_TOO_HIGH, 1, 10);

    EXPECT_TRUE(pdu.is_valid());
    EXPECT_TRUE(pdu.common.is_response());
    EXPECT_EQ(pdu.common.message_type(), AECP_MESSAGE_TYPE_ADDRESS_ACCESS_RESPONSE);
    EXPECT_EQ(pdu.common.status(), AA_STATUS_ADDRESS_TOO_HIGH);
    EXPECT_EQ(pdu.tlv_count.get(), 1u);
}

// ---------------------------------------------------------------------------
// TLV parsing
// ---------------------------------------------------------------------------

TEST(aecp_aa, parse_single_read_tlv)
{
    // Build a single READ TLV: mode=0, length=4, address=0x0000000000001000, data=0000
    std::array<uint8_t, 14> tlv_data = {
        0x00,
        0x04,  // mode=0(READ), length=4
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x10,
        0x00,  // address=0x1000
        0x00,
        0x00,
        0x00,
        0x00,  // memory_data (4 zero bytes)
    };

    uint16_t cb_count = 0;
    uint8_t cb_mode = 0xFF;
    uint64_t cb_address = 0;
    size_t cb_data_len = 0;

    auto result = aa_parse_tlvs(1, tlv_data, [&](uint16_t, uint8_t mode, uint64_t address, std::span<uint8_t const> data) {
        cb_count++;
        cb_mode = mode;
        cb_address = address;
        cb_data_len = data.size();
    });

    EXPECT_TRUE(result);
    EXPECT_EQ(cb_count, 1u);
    EXPECT_EQ(cb_mode, AA_MODE_READ);
    EXPECT_EQ(cb_address, 0x1000u);
    EXPECT_EQ(cb_data_len, 4u);
}

TEST(aecp_aa, parse_single_write_tlv)
{
    std::array<uint8_t, 14> tlv_data = {
        0x10,
        0x04,  // mode=1(WRITE), length=4
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x20,
        0x00,  // address=0x2000
        0xDE,
        0xAD,
        0xBE,
        0xEF,  // memory_data
    };

    uint8_t cb_mode = 0xFF;
    uint64_t cb_address = 0;
    std::vector<uint8_t> cb_data;

    auto result = aa_parse_tlvs(1, tlv_data, [&](uint16_t, uint8_t mode, uint64_t address, std::span<uint8_t const> data) {
        cb_mode = mode;
        cb_address = address;
        cb_data.assign(data.begin(), data.end());
    });

    EXPECT_TRUE(result);
    EXPECT_EQ(cb_mode, AA_MODE_WRITE);
    EXPECT_EQ(cb_address, 0x2000u);
    EXPECT_EQ(cb_data.size(), 4u);
    EXPECT_EQ(cb_data[0], 0xDE);
    EXPECT_EQ(cb_data[1], 0xAD);
    EXPECT_EQ(cb_data[2], 0xBE);
    EXPECT_EQ(cb_data[3], 0xEF);
}

TEST(aecp_aa, parse_multiple_tlvs)
{
    // Two TLVs: READ(4 bytes at 0x1000) + WRITE(2 bytes at 0x2000)
    std::array<uint8_t, 26> tlv_data = {
        // TLV 0: READ, length=4, address=0x1000
        0x00,
        0x04,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x10,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        // TLV 1: WRITE, length=2, address=0x2000
        0x10,
        0x02,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x20,
        0x00,
        0xCA,
        0xFE,
    };

    uint16_t count = 0;
    std::array<uint8_t, 2> modes{};
    std::array<uint64_t, 2> addresses{};

    auto result = aa_parse_tlvs(2, tlv_data, [&](uint16_t idx, uint8_t mode, uint64_t address, std::span<uint8_t const>) {
        if (idx < 2) {
            modes[idx] = mode;
            addresses[idx] = address;
        }
        count++;
    });

    EXPECT_TRUE(result);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(modes[0], AA_MODE_READ);
    EXPECT_EQ(modes[1], AA_MODE_WRITE);
    EXPECT_EQ(addresses[0], 0x1000u);
    EXPECT_EQ(addresses[1], 0x2000u);
}

TEST(aecp_aa, parse_truncated_tlv_fails)
{
    // Only 5 bytes — not enough for a TLV header (needs 10)
    std::array<uint8_t, 5> tlv_data = {0x00, 0x04, 0x00, 0x00, 0x00};

    auto result = aa_parse_tlvs(1, tlv_data, [](uint16_t, uint8_t, uint64_t, std::span<uint8_t const>) {});
    EXPECT_FALSE(result);
}

TEST(aecp_aa, parse_data_overflow_fails)
{
    // TLV header says length=100 but only 4 bytes available after header
    std::array<uint8_t, 14> tlv_data = {
        0x00,
        0x64,  // mode=READ, length=100
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x10,
        0x00,  // address
        0x00,
        0x00,
        0x00,
        0x00,  // only 4 bytes data
    };

    auto result = aa_parse_tlvs(1, tlv_data, [](uint16_t, uint8_t, uint64_t, std::span<uint8_t const>) {});
    EXPECT_FALSE(result);
}

TEST(aecp_aa, parse_zero_length_tlv)
{
    // EXECUTE with length=0
    std::array<uint8_t, 10> tlv_data = {
        0x20,
        0x00,  // mode=2(EXECUTE), length=0
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x30,
        0x00,  // address=0x3000
    };

    uint8_t cb_mode = 0xFF;
    size_t cb_data_len = 99;

    auto result = aa_parse_tlvs(1, tlv_data, [&](uint16_t, uint8_t mode, uint64_t, std::span<uint8_t const> data) {
        cb_mode = mode;
        cb_data_len = data.size();
    });

    EXPECT_TRUE(result);
    EXPECT_EQ(cb_mode, AA_MODE_EXECUTE);
    EXPECT_EQ(cb_data_len, 0u);
}

// ---------------------------------------------------------------------------
// TLV building
// ---------------------------------------------------------------------------

TEST(aecp_aa, builder_read)
{
    AaTlvBuilder builder;
    builder.add_read(0x1000, 8);

    EXPECT_EQ(builder.tlv_count(), 1u);
    // TLV header (10) + data (8) = 18 bytes
    EXPECT_EQ(builder.tlv_data().size(), 18u);
}

TEST(aecp_aa, builder_write)
{
    std::array<uint8_t, 4> data = {0xDE, 0xAD, 0xBE, 0xEF};
    AaTlvBuilder builder;
    builder.add_write(0x2000, data);

    EXPECT_EQ(builder.tlv_count(), 1u);
    EXPECT_EQ(builder.tlv_data().size(), 14u);  // 10 + 4
}

TEST(aecp_aa, builder_roundtrip)
{
    // Build a command with READ + WRITE
    std::array<uint8_t, 4> write_data = {0xCA, 0xFE, 0xBA, 0xBE};

    AaTlvBuilder builder;
    builder.add_read(0x1000, 4);
    builder.add_write(0x2000, write_data);

    Eui64 const target{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    Eui64 const controller{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x01};

    auto frame = builder.build_command(target, controller, 99);

    // Parse header
    EXPECT_TRUE(frame.size() >= AecpAaDu::LENGTH);
    AecpAaDu pdu{};
    span_load(pdu, make_const_span(frame).first<AecpAaDu::LENGTH>());
    EXPECT_TRUE(pdu.is_valid());
    EXPECT_EQ(pdu.tlv_count.get(), 2u);
    EXPECT_EQ(pdu.common.sequence_id.get(), 99u);

    // Parse TLVs
    auto tlv_payload = make_const_span(frame).subspan(AecpAaDu::LENGTH);
    uint16_t count = 0;
    std::array<uint8_t, 2> modes{};
    std::array<uint64_t, 2> addresses{};

    auto result = aa_parse_tlvs(
        pdu.tlv_count.get(), tlv_payload, [&](uint16_t idx, uint8_t mode, uint64_t addr, std::span<uint8_t const> data) {
            if (idx < 2) {
                modes[idx] = mode;
                addresses[idx] = addr;
            }
            if (idx == 1) {
                // Verify write data round-tripped
                EXPECT_EQ(data.size(), 4u);
                EXPECT_EQ(data[0], 0xCA);
                EXPECT_EQ(data[1], 0xFE);
                EXPECT_EQ(data[2], 0xBA);
                EXPECT_EQ(data[3], 0xBE);
            }
            count++;
        });

    EXPECT_TRUE(result);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(modes[0], AA_MODE_READ);
    EXPECT_EQ(modes[1], AA_MODE_WRITE);
    EXPECT_EQ(addresses[0], 0x1000u);
    EXPECT_EQ(addresses[1], 0x2000u);
}

TEST(aecp_aa, builder_response_roundtrip)
{
    std::array<uint8_t, 8> read_data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    AaTlvBuilder builder;
    builder.add_write(0x1000, read_data);  // response carries data for READs via WRITE-mode TLVs

    Eui64 const target{};
    Eui64 const controller{};

    auto frame = builder.build_response(target, controller, 5, AA_STATUS_SUCCESS);

    AecpAaDu pdu{};
    span_load(pdu, make_const_span(frame).first<AecpAaDu::LENGTH>());
    EXPECT_TRUE(pdu.is_valid());
    EXPECT_TRUE(pdu.common.is_response());
    EXPECT_EQ(pdu.common.status(), AA_STATUS_SUCCESS);
}

TEST(aecp_aa, builder_clear)
{
    AaTlvBuilder builder;
    builder.add_read(0x1000, 4);
    EXPECT_EQ(builder.tlv_count(), 1u);

    builder.clear();
    EXPECT_EQ(builder.tlv_count(), 0u);
    EXPECT_TRUE(builder.tlv_data().empty());
}

TEST(aecp_aa, parse_large_address)
{
    // Address with all bits set: 0xFFFFFFFFFFFFFFFF
    std::array<uint8_t, 10> tlv_data = {
        0x00,
        0x00,  // mode=READ, length=0
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,  // address=max
    };

    uint64_t cb_address = 0;
    auto result =
        aa_parse_tlvs(1, tlv_data, [&](uint16_t, uint8_t, uint64_t address, std::span<uint8_t const>) { cb_address = address; });

    EXPECT_TRUE(result);
    EXPECT_EQ(cb_address, UINT64_MAX);
}

TEST_MAIN(statusbar_atdecc, atdecc_aecp_aa_test)
