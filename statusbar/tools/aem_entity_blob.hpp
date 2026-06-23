#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// @file aem_entity_blob.hpp
/// @brief Writer for the atdecc::aem::DescriptorStorage "AEM1" blob format.
///
/// AemEntityBlob serializes a set of AEM descriptors into the on-disk
/// descriptor-storage layout — a DescriptorStorageHeader, a table of contents
/// (one DescriptorStorageTocEntry per descriptor, keyed by configuration /
/// type / index), then the concatenated descriptor wire bytes. No symbol
/// table is emitted; the entity loader resolves descriptors by
/// (configuration, type, index) via the TOC.
///
/// The matching reader is atdecc::aem::DescriptorStorage. This builder lives in
/// a header (rather than buried in aem_entity_blob_tool.cpp) so it can be unit
/// tested and reused.

#include "statusbar/atdecc/atdecc_descriptor_storage.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace statusbar::tools {

/// Wire length of a descriptor: its variable-trailer-aware wire_size().
template <typename T>
[[nodiscard]] auto descriptor_wire_bytes(T const& d) -> std::vector<uint8_t>
{
    size_t n = 0;
    if constexpr (requires { d.wire_size(); }) {
        n = d.wire_size();
    } else {
        n = T::LENGTH;
    }
    // The fixed header is followed in-memory by the trailer array at offset
    // LENGTH, so the first wire_size() bytes are exactly the on-wire form.
    std::vector<uint8_t> v(n);
    std::memcpy(v.data(), &d, n);
    return v;
}

class AemEntityBlob
{
  public:
    template <typename T>
    void add(uint16_t configuration, T const& desc)
    {
        entries_.push_back(Entry{
            .type = static_cast<uint16_t>(desc.descriptor_type.get()),
            .index = static_cast<uint16_t>(desc.descriptor_index.get()),
            .config = configuration,
            .bytes = descriptor_wire_bytes(desc),
        });
    }

    [[nodiscard]] auto build() const -> std::vector<uint8_t>
    {
        using atdecc::aem::DescriptorStorage;
        using atdecc::aem::DescriptorStorageHeader;
        using atdecc::aem::DescriptorStorageTocEntry;

        size_t const toc_offset = DescriptorStorageHeader::LENGTH;  // 20
        size_t const desc_base = toc_offset + (entries_.size() * DescriptorStorageTocEntry::LENGTH);
        size_t total = desc_base;
        for (auto const& e : entries_) {
            total += e.bytes.size();
        }

        std::vector<uint8_t> blob(total, 0);

        DescriptorStorageHeader header{};
        header.magic = DescriptorStorage::MAGIC;
        header.toc_count = static_cast<uint32_t>(entries_.size());
        header.toc_offset = static_cast<uint32_t>(toc_offset);
        header.symbol_count = 0;
        header.symbol_offset = static_cast<uint32_t>(desc_base);  // empty table at desc base
        std::memcpy(blob.data(), &header, DescriptorStorageHeader::LENGTH);

        size_t off = desc_base;
        for (size_t i = 0; i < entries_.size(); ++i) {
            auto const& e = entries_[i];
            DescriptorStorageTocEntry toc{};
            toc.descriptor_type = e.type;
            toc.descriptor_index = e.index;
            toc.configuration_index = e.config;
            toc.length = static_cast<uint16_t>(e.bytes.size());
            toc.offset = static_cast<uint32_t>(off);
            std::memcpy(
                blob.data() + toc_offset + (i * DescriptorStorageTocEntry::LENGTH), &toc, DescriptorStorageTocEntry::LENGTH);
            std::memcpy(blob.data() + off, e.bytes.data(), e.bytes.size());
            off += e.bytes.size();
        }
        return blob;
    }

  private:
    struct Entry
    {
        uint16_t type;
        uint16_t index;
        uint16_t config;
        std::vector<uint8_t> bytes;
    };
    std::vector<Entry> entries_;
};

}  // namespace statusbar::tools
