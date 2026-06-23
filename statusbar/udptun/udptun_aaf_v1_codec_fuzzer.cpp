// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for udptun::AafV1OverAnnexJCodec::decode. The udptun
/// codec parses WAN-untrusted datagrams arriving on the inter-site audio
/// tunnel, so it must never crash, read out of bounds, or trip UB on any
/// input. decode() may legally return std::nullopt; we drive the accessors
/// on every successfully-decoded packet to fuzz those paths too. OOB/UB is
/// surfaced via ASan/UBSan.

#include "statusbar/ieee/ieee.hpp"
#include "statusbar/udptun/udptun_aaf_v1_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    statusbar::udptun::AafV1OverAnnexJCodec const codec{};

    auto const bytes = std::span<uint8_t const>(data, size);
    auto const decoded = codec.decode(bytes);
    if (decoded.has_value()) {
        auto const& p = *decoded;
        // Exercise the read-only accessors used on the hot RX path.
        statusbar::ieee::Eui64 const probe{};
        (void)codec.sender_id(p);
        (void)codec.sender_pair_id(p);
        (void)codec.sequence(p);
        (void)codec.tx_gptp_ns(p);
        (void)codec.announced_interval_us(p);
        (void)codec.classify(p, probe);
        (void)p.audio.size();
    }
    (void)codec.validate_for_reflect(bytes);
    return 0;
}
