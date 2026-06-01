#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// OWLM-specific TxIdentityPair construction helper. Bridges the
/// EUI-64 identity scheme defined in `owlm_eui64.hpp` to the generic
/// `udptun::TxIdentityPair` consumed by `udptun::Session<C>`.
///
/// Linux-gated because `udptun::TxIdentityPair` lives in a Linux-only
/// header (`udptun_identity.hpp` pulls in gptp::SlaveSession).

#if defined(__linux__)

#    include "statusbar/ieee/ieee.hpp"
#    include "statusbar/owlm/owlm_eui64.hpp"
#    include "statusbar/udptun/udptun_identity.hpp"

#    include <cstdint>
#    include <utility>

namespace statusbar::owlm {

/// CLI-shaped knobs for `build_tx_identity`. `redundant=true` activates
/// the dual-stream primary/redundant pair (default OWLM behaviour);
/// `legacy_mid` is consulted only when `redundant=false` and selects the
/// EUI-64 mid bytes for the single-stream backwards-compat sender.
struct TxIdentityParams
{
    bool redundant{true};
    uint16_t legacy_mid{0xFFFE};
    int64_t temporal_shift_ms{10};
};

/// Build the `(TxIdentityPair, pair_id)` tuple used by
/// `udptun::Session<C>::IdentityBuilder`. The pair_id is the canonical
/// logical-sender id (mid bytes zeroed) so primary and redundant copies
/// of the same logical sender both classify as self-loopback.
[[nodiscard]] inline auto build_tx_identity(ieee::Eui48 const& mac, TxIdentityParams const& p) noexcept
    -> std::pair<udptun::TxIdentityPair, ieee::Eui64>
{
    udptun::TxIdentityPair s{};
    if (p.redundant) {
        s.primary_id = make_owlm_eui64(mac, PRIMARY_MID_BYTES);
        s.redundant_id = make_owlm_eui64(mac, REDUNDANT_MID_BYTES);
        s.redundant_enabled = true;
        s.temporal_shift_ns = p.temporal_shift_ms * 1'000'000;
    } else {
        s.primary_id = make_owlm_eui64(mac, p.legacy_mid);
        s.redundant_id = s.primary_id;
        s.redundant_enabled = false;
        s.temporal_shift_ns = 0;
    }
    return {s, eui64_pair_id(s.primary_id)};
}

}  // namespace statusbar::owlm

#endif  // __linux__
