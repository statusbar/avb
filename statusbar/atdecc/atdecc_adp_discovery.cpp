// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// ADP Discovery State Machine action implementations

#include "statusbar/atdecc/atdecc_adp_discovery.hpp"

namespace statusbar::atdecc::discovery_actions {

void handle_available(DiscoveryContext& ctx, sm::TimePoint event_time)
{
    auto result = ctx.entities.process_available(ctx.rcvd_adpdu, ctx.rcvd_src_mac, event_time);

    switch (result) {
        case AdpProcessResult::Added:
        case AdpProcessResult::Full: {
            auto const* entity = ctx.entities.find(ctx.rcvd_adpdu.entity_id);
            if (entity != nullptr && ctx.on_entity_available) {
                ctx.on_entity_available(*entity);
            }
            break;
        }
        case AdpProcessResult::Updated: {
            auto const* entity = ctx.entities.find(ctx.rcvd_adpdu.entity_id);
            if (entity != nullptr && ctx.on_entity_updated) {
                ctx.on_entity_updated(*entity);
            }
            break;
        }
        case AdpProcessResult::Unchanged:
            break;
    }
}

void handle_departing(DiscoveryContext& ctx, sm::TimePoint /*event_time*/)
{
    auto const entity_id = ctx.rcvd_adpdu.entity_id;
    if (ctx.entities.process_departing(entity_id)) {
        if (ctx.on_entity_departing) {
            ctx.on_entity_departing(entity_id);
        }
    }
}

void send_discover(DiscoveryContext& ctx, sm::TimePoint /*event_time*/)
{
    if (!ctx.tx_discover) {
        return;
    }

    AdpDu discover{};
    discover.init_entity_discover();
    discover.entity_id = ctx.discover_target;
    ctx.tx_discover(discover);
}

}  // namespace statusbar::atdecc::discovery_actions
