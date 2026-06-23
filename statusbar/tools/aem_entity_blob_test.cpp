// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for the AemEntityBlob descriptor-storage writer
// (statusbar/tools/aem_entity_blob.hpp). It serializes AEM descriptors into the
// "AEM1" DescriptorStorage blob consumed by the entity loader; verify the
// output round-trips through the atdecc::aem::DescriptorStorage reader — fixed-
// and variable-size descriptors, lookup by (configuration, type, index), and
// the empty blob. Previously the builder had no test (it lived in the tool
// .cpp); this also exercises the Tier-2 silent-truncation fix's success path.

#include "statusbar/tools/aem_entity_blob.hpp"

#include "statusbar/atdecc/atdecc_aem_descriptor.hpp"
#include "statusbar/atdecc/atdecc_descriptor_storage.hpp"
#include "statusbar/test/test.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

using statusbar::tools::AemEntityBlob;
using statusbar::tools::descriptor_wire_bytes;
namespace aem = statusbar::atdecc::aem;

namespace {

auto as_span(std::vector<uint8_t> const& v) -> std::span<uint8_t const>
{
    return {v.data(), v.size()};
}

}  // namespace

TEST(aem_entity_blob, empty_blob_is_valid_with_no_descriptors)
{
    AemEntityBlob b;
    auto const blob = b.build();

    auto storage = aem::DescriptorStorage::create(as_span(blob));
    EXPECT_TRUE(storage.has_value());
    EXPECT_EQ(storage->get_configuration_count(), uint16_t{0});
    // Nothing to look up.
    EXPECT_FALSE(storage->get_descriptor(0, aem::DESCRIPTOR_ENTITY, 0).has_value());
}

TEST(aem_entity_blob, fixed_size_descriptor_roundtrips)
{
    aem::DescriptorEntity ent{};
    ent.entity_name = aem::AtdeccString{"unit-test-entity"};

    AemEntityBlob b;
    b.add(0, ent);
    auto const blob = b.build();

    auto storage = aem::DescriptorStorage::create(as_span(blob));
    EXPECT_TRUE(storage.has_value());
    EXPECT_EQ(storage->get_configuration_count(), uint16_t{1});

    auto got = storage->get_descriptor(0, aem::DESCRIPTOR_ENTITY, 0);
    EXPECT_TRUE(got.has_value());

    auto const expected = descriptor_wire_bytes(ent);
    EXPECT_EQ(got->size(), expected.size());  // 312, the fixed ENTITY length
    EXPECT_EQ(got->size(), aem::DescriptorEntity::LENGTH);
    EXPECT_TRUE(std::ranges::equal(*got, expected));
}

TEST(aem_entity_blob, multiple_descriptors_looked_up_by_key)
{
    aem::DescriptorEntity ent{};
    aem::DescriptorConfiguration cfg{};
    cfg.object_name = aem::AtdeccString{"config-0"};

    AemEntityBlob b;
    b.add(0, ent);
    b.add(0, cfg);
    auto const blob = b.build();

    auto storage = aem::DescriptorStorage::create(as_span(blob));
    EXPECT_TRUE(storage.has_value());

    auto e = storage->get_descriptor(0, aem::DESCRIPTOR_ENTITY, 0);
    auto c = storage->get_descriptor(0, aem::DESCRIPTOR_CONFIGURATION, 0);
    EXPECT_TRUE(e.has_value());
    EXPECT_TRUE(c.has_value());
    EXPECT_TRUE(std::ranges::equal(*e, descriptor_wire_bytes(ent)));
    EXPECT_TRUE(std::ranges::equal(*c, descriptor_wire_bytes(cfg)));

    // Keys that were never added must miss, not alias another descriptor.
    EXPECT_FALSE(storage->get_descriptor(0, aem::DESCRIPTOR_ENTITY, 7).has_value());
    EXPECT_FALSE(storage->get_descriptor(1, aem::DESCRIPTOR_ENTITY, 0).has_value());
}

TEST(aem_entity_blob, variable_size_descriptor_trailer_roundtrips)
{
    // A CONFIGURATION descriptor with a populated descriptor_counts trailer:
    // wire_size() grows past the 74-byte fixed header, so the builder must
    // carry the variable tail through to the stored bytes.
    aem::DescriptorConfiguration cfg{};
    EXPECT_TRUE(cfg.push_descriptor_count({.descriptor_type = aem::DESCRIPTOR_AUDIO_UNIT, .count = 1}));
    EXPECT_TRUE(cfg.push_descriptor_count({.descriptor_type = aem::DESCRIPTOR_STREAM_INPUT, .count = 2}));
    EXPECT_TRUE(cfg.wire_size() > aem::DescriptorConfiguration::LENGTH);

    AemEntityBlob b;
    b.add(0, cfg);
    auto const blob = b.build();

    auto storage = aem::DescriptorStorage::create(as_span(blob));
    EXPECT_TRUE(storage.has_value());

    auto got = storage->get_descriptor(0, aem::DESCRIPTOR_CONFIGURATION, 0);
    EXPECT_TRUE(got.has_value());
    EXPECT_EQ(got->size(), cfg.wire_size());
    EXPECT_TRUE(std::ranges::equal(*got, descriptor_wire_bytes(cfg)));
}

// Test runner

TEST_MAIN(statusbar_tools, aem_entity_blob_test)
