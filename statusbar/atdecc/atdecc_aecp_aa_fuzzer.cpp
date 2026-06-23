// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for atdecc::aa_parse_tlvs. The AECP Address Access
/// command carries a wire-supplied tlv_count followed by variable-length
/// TLVs; the parser walks them against an attacker-controlled buffer and must
/// never read out of bounds. We take the first two bytes as the (untrusted)
/// tlv_count and feed the remainder as the TLV data, with a callback that
/// touches every parsed field. OOB/UB is surfaced via ASan/UBSan.

#include "statusbar/atdecc/atdecc_aecp_aa.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    uint16_t tlv_count = 0;
    std::span<uint8_t const> tlv_data{};
    if (size >= 2) {
        tlv_count = static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
        tlv_data = std::span<uint8_t const>(data + 2, size - 2);
    }

    (void)statusbar::atdecc::aa_parse_tlvs(
        tlv_count, tlv_data, [](uint16_t index, uint8_t mode, uint64_t address, std::span<uint8_t const> mem) {
            (void)index;
            (void)mode;
            (void)address;
            (void)mem.size();
        });
    return 0;
}
