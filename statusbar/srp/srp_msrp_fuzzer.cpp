// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for MSRP ingress parsing. Feeds arbitrary bytes to
/// MsrpParticipant::receive_pdu, which walks the MRPDU attribute/vector
/// structure. A fresh participant per input keeps single-input replay
/// reproducible. Looks for OOB/UB via ASan/UBSan.

#include "statusbar/srp/srp_msrp_participant.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    using statusbar::srp::msrp::MsrpConfig;
    using statusbar::srp::msrp::MsrpParticipant;

    MsrpParticipant participant{MsrpConfig{}, 0};
    auto const now = statusbar::sm::TimePoint{} + std::chrono::seconds(1);
    participant.receive_pdu(std::span<uint8_t const>(data, size), now);
    return 0;
}
