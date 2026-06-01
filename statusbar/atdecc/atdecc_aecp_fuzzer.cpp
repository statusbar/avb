// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for AECP common header and Address-Access PDU
/// deserialization via length-checked load().

#include "statusbar/atdecc/atdecc_aecp.hpp"
#include "statusbar/atdecc/atdecc_aecp_aa.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const packet = std::span<uint8_t const>(data, size);

    statusbar::atdecc::AecpDuCommon common{};
    if (statusbar::protocol::load(packet, &common)) {
        (void)common.message_type();
        (void)common.is_response();
    }

    statusbar::atdecc::AecpAaDu aa{};
    if (statusbar::protocol::load(packet, &aa)) {
        auto const sink = aa;
        (void)sink;
    }
    return 0;
}
