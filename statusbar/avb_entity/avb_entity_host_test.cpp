// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Tests for AvbEntityHost's symbol-aware construction path: building the host from
// a DescriptorStorageHandler (instead of a parsed EntityModel) and the
// symbol <-> descriptor lookups it then exposes. The legacy parsed-model path
// returns empty/failure from those lookups.

#include "statusbar/avb_entity/avb_entity_host.hpp"

#include "statusbar/atdecc/atdecc_aem_descriptor.hpp"
#include "statusbar/atdecc/atdecc_descriptor_storage.hpp"
#include "statusbar/nanoavb/nanoavb_aem_descriptor_storage_handler.hpp"
#include "statusbar/test/test.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

using statusbar::atdecc::aem::DESCRIPTOR_ENTITY;
using statusbar::atdecc::aem::DescriptorEntity;
using statusbar::atdecc::aem::DescriptorStorage;
using statusbar::avb_entity::AvbEntityHost;
using statusbar::nanoavb::AdpAdvertiserConfig;
using statusbar::nanoavb::DescriptorStorageHandler;
using statusbar::nanoavb::EntityModel;

namespace {

/// Minimal .aem blob: one ENTITY descriptor (config 0, index 0) plus one symbol
/// (0xC0FFEE) bound to it. (Same byte layout as nanoavb's storage tests.)
auto make_entity_blob() -> std::vector<uint8_t>
{
    constexpr uint32_t header_size = 20;
    constexpr uint32_t toc_entry_size = 12;
    constexpr uint32_t symbol_entry_size = 10;
    constexpr uint32_t desc_size = DescriptorEntity::LENGTH;  // 312
    constexpr uint32_t toc_offset = header_size;
    constexpr uint32_t symbol_offset = toc_offset + toc_entry_size;
    constexpr uint32_t desc_offset = symbol_offset + symbol_entry_size;
    constexpr uint32_t total = desc_offset + desc_size;

    std::vector<uint8_t> blob(total, 0);
    blob[0] = 0x41;  // "AEM1"
    blob[1] = 0x45;
    blob[2] = 0x4D;
    blob[3] = 0x31;
    blob[7] = 0x01;                                  // toc_count = 1
    blob[11] = static_cast<uint8_t>(toc_offset);     // toc_offset = 20
    blob[15] = 0x01;                                 // symbol_count = 1
    blob[19] = static_cast<uint8_t>(symbol_offset);  // symbol_offset = 32

    // TOC entry: type=ENTITY(0), index=0, config=0, length=312, offset=desc_offset
    blob[toc_offset + 6] = static_cast<uint8_t>((desc_size >> 8) & 0xFF);
    blob[toc_offset + 7] = static_cast<uint8_t>(desc_size & 0xFF);
    blob[toc_offset + 11] = static_cast<uint8_t>(desc_offset);

    // Symbol entry: type=ENTITY, index=0, config=0, symbol=0x00C0FFEE
    blob[symbol_offset + 7] = 0xC0;
    blob[symbol_offset + 8] = 0xFF;
    blob[symbol_offset + 9] = 0xEE;

    // A real DescriptorEntity in the slot.
    DescriptorEntity desc{};
    desc.configurations_count = 1;
    std::memcpy(blob.data() + desc_offset, &desc, sizeof(desc));
    return blob;
}

auto make_host_from_blob(std::vector<uint8_t> const& blob) -> std::unique_ptr<AvbEntityHost>
{
    auto storage = DescriptorStorage::create(std::span<uint8_t const>(blob));
    EXPECT_TRUE(storage.has_value());
    auto handler = std::make_unique<DescriptorStorageHandler>(*storage);
    return std::make_unique<AvbEntityHost>(std::move(handler), AdpAdvertiserConfig{}, 1, 4, 1);
}

}  // namespace

TEST(avb_entity_host_symbol, constructs_symbol_aware_and_resolves_forward)
{
    auto blob = make_entity_blob();
    auto host = make_host_from_blob(blob);

    auto sym = host->symbol_of(DESCRIPTOR_ENTITY, 0);
    EXPECT_TRUE(sym.has_value());
    EXPECT_EQ(*sym, 0x00C0FFEEu);
}

TEST(avb_entity_host_symbol, resolves_reverse)
{
    auto blob = make_entity_blob();
    auto host = make_host_from_blob(blob);

    auto loc = host->descriptor_for_symbol(0x00C0FFEEu);
    EXPECT_TRUE(loc.has_value());
    EXPECT_EQ(static_cast<uint16_t>(loc->descriptor_type), static_cast<uint16_t>(DESCRIPTOR_ENTITY));
    EXPECT_EQ(static_cast<uint16_t>(loc->descriptor_index), 0u);
}

TEST(avb_entity_host_symbol, get_descriptor_returns_blob_bytes)
{
    auto blob = make_entity_blob();
    auto host = make_host_from_blob(blob);

    auto desc = host->get_descriptor(DESCRIPTOR_ENTITY, 0);
    EXPECT_TRUE(desc.has_value());
    EXPECT_EQ(desc->size(), static_cast<size_t>(DescriptorEntity::LENGTH));
}

TEST(avb_entity_host_symbol, unknown_symbol_and_descriptor_fail)
{
    auto blob = make_entity_blob();
    auto host = make_host_from_blob(blob);

    EXPECT_FALSE(host->symbol_of(DESCRIPTOR_ENTITY, 7).has_value());  // no such index
    EXPECT_FALSE(host->descriptor_for_symbol(0x12345678u).has_value());
    EXPECT_FALSE(host->get_descriptor(DESCRIPTOR_ENTITY, 7).has_value());
}

TEST(avb_entity_host_symbol, legacy_parsed_model_path_has_no_storage)
{
    // The legacy (EntityModel) ctor leaves the host with no backing blob, so the
    // symbol lookups return empty/failure.
    AvbEntityHost host{EntityModel{}, AdpAdvertiserConfig{}, 1, 4, 1};
    EXPECT_EQ(host.descriptor_storage(), nullptr);
    EXPECT_FALSE(host.symbol_of(DESCRIPTOR_ENTITY, 0).has_value());
    EXPECT_FALSE(host.descriptor_for_symbol(0x00C0FFEEu).has_value());
}

TEST_MAIN(statusbar_avb_entity, avb_entity_host_test)
