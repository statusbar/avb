// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for owlm::decode_owlm_packet. OWLM measurement packets
/// arrive over UDP from an untrusted source, so the decoder must not crash,
/// read out of bounds, or trip UB on any input. A non-empty error_code on
/// malformed input is the expected (safe) outcome; OOB/UB is caught by
/// ASan/UBSan.

#include "statusbar/owlm/owlm_packet.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto const buf = std::span<uint8_t const>(data, size);
    statusbar::owlm::OwlmPacket out{};
    auto const ec = statusbar::owlm::decode_owlm_packet(buf, out);
    if (!ec) {
        // Touch the decoded fields so any miscomputed offset is observable.
        (void)out.sender_eui64.to_uint64();
        (void)out.sequence;
        (void)out.tx_gptp_ns;
        (void)out.tx_interval_us;
    }
    return 0;
}
