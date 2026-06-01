// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for AEM command/response payload parsing. Loads the
/// fixed AemDu header (length-checked), then runs parse_aem() over the trailing
/// payload with the header-derived command code and direction. Covers both AEM
/// commands and responses.

#include "statusbar/atdecc/atdecc_aecp_aem.hpp"
#include "statusbar/atdecc/atdecc_aem_format.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const packet = std::span<uint8_t const>(data, size);
    statusbar::atdecc::AemDu hdr{};
    if (statusbar::protocol::load(packet, &hdr)) {
        // load() succeeded => packet.size() >= AemDu::LENGTH, so subspan is valid.
        auto const payload = packet.subspan(statusbar::atdecc::AemDu::LENGTH);
        auto const parsed = statusbar::atdecc::parse_aem(hdr.command_code(), hdr.is_response(), payload);
        (void)parsed;
    }
    return 0;
}
