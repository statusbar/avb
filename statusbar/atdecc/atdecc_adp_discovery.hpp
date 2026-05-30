#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// ADP Discovery State Machine - IEEE 1722.1 Clause 6.2.4
/// Manages a database of discovered ATDECC entities

#include "statusbar/atdecc/atdecc_adp.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/sg14/inplace_function.h"
#include "statusbar/sm/sm.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace statusbar::atdecc {

//
// Discovered Entity Record
//

/// Information about a discovered ATDECC entity
struct DiscoveredEntity
{
    AdpDu adpdu{};                ///< Complete ADPDU with all entity fields
    ieee::Eui48 source_mac{};     ///< MAC address of sender (for unicast AEM)
    sm::TimePoint last_seen{};    ///< When last ENTITY_AVAILABLE was received
    sm::TimePoint expiry_time{};  ///< last_seen + valid_time*2 seconds
    uint32_t last_available_index{0};
    bool valid{false};
};

//
// Entity Database
//

/// Result of processing an ENTITY_AVAILABLE message
enum class AdpProcessResult : uint8_t
{
    Added,      ///< New entity added to database
    Updated,    ///< Existing entity updated (available_index changed)
    Unchanged,  ///< Entity already known, no change
    Full,       ///< Database full, oldest entity evicted and new one added
};

/// Fixed-size database of discovered ATDECC entities
/// @tparam MaxEntities Maximum number of entities that can be tracked
template <size_t MaxEntities = 64>
class AdpEntityDatabase
{
  public:
    /// Process an ENTITY_AVAILABLE message
    auto process_available(AdpDu const& adp, ieee::Eui48 const& src_mac, sm::TimePoint now) -> AdpProcessResult
    {
        auto const entity_id = adp.entity_id;
        auto const valid_time_seconds = static_cast<int>(adp.valid_time()) * 2;
        auto const expiry = now + std::chrono::seconds(valid_time_seconds);

        // Check if entity already exists
        for (auto& entry : entities_) {
            if (entry.valid && entry.adpdu.entity_id == entity_id) {
                bool const index_changed = adp.available_index.get() != entry.last_available_index;
                entry.adpdu = adp;
                entry.source_mac = src_mac;
                entry.last_seen = now;
                entry.expiry_time = expiry;
                entry.last_available_index = adp.available_index.get();
                return index_changed ? AdpProcessResult::Updated : AdpProcessResult::Unchanged;
            }
        }

        // Find empty slot
        for (auto& entry : entities_) {
            if (!entry.valid) {
                entry = {
                    .adpdu = adp,
                    .source_mac = src_mac,
                    .last_seen = now,
                    .expiry_time = expiry,
                    .last_available_index = adp.available_index.get(),
                    .valid = true,
                };
                return AdpProcessResult::Added;
            }
        }

        // Database full - evict oldest entity
        size_t oldest_idx = 0;
        auto oldest_time = entities_[0].last_seen;
        for (size_t i = 1; i < MaxEntities; ++i) {
            if (entities_[i].valid && entities_[i].last_seen < oldest_time) {
                oldest_idx = i;
                oldest_time = entities_[i].last_seen;
            }
        }
        entities_[oldest_idx] = {
            .adpdu = adp,
            .source_mac = src_mac,
            .last_seen = now,
            .expiry_time = expiry,
            .last_available_index = adp.available_index.get(),
            .valid = true,
        };
        return AdpProcessResult::Full;
    }

    /// Process an ENTITY_DEPARTING message
    /// @return true if entity was found and removed
    auto process_departing(ieee::Eui64 const& entity_id) -> bool
    {
        for (auto& entry : entities_) {
            if (entry.valid && entry.adpdu.entity_id == entity_id) {
                entry.valid = false;
                entry = {};
                return true;
            }
        }
        return false;
    }

    /// Remove entities whose valid_time has expired
    /// @param fn Called for each expired entity before removal
    template <typename F>
    void expire_stale(sm::TimePoint now, F const& fn)
    {
        for (auto& entry : entities_) {
            if (entry.valid && now >= entry.expiry_time) {
                fn(entry);
                entry.valid = false;
                entry = {};
            }
        }
    }

    /// Find an entity by entity_id
    [[nodiscard]] auto find(ieee::Eui64 const& entity_id) const -> DiscoveredEntity const*
    {
        for (auto const& entry : entities_) {
            if (entry.valid && entry.adpdu.entity_id == entity_id) {
                return &entry;
            }
        }
        return nullptr;
    }

    /// Get the MAC address for a known entity
    [[nodiscard]] auto mac_for_entity(ieee::Eui64 const& entity_id) const -> std::optional<ieee::Eui48>
    {
        auto const* e = find(entity_id);
        if (e != nullptr) {
            return e->source_mac;
        }
        return std::nullopt;
    }

    /// Count of valid entries
    [[nodiscard]] auto count() const -> size_t
    {
        size_t n = 0;
        for (auto const& entry : entities_) {
            if (entry.valid) {
                ++n;
            }
        }
        return n;
    }

    /// Maximum capacity
    [[nodiscard]] static constexpr auto capacity() -> size_t { return MaxEntities; }

    /// Iterate over valid entries
    template <typename F>
    void for_each(F const& fn) const
    {
        for (auto const& entry : entities_) {
            if (entry.valid) {
                fn(entry);
            }
        }
    }

    /// Iterate over entities with talker capabilities
    template <typename F>
    void for_each_talker(F const& fn) const
    {
        for (auto const& entry : entities_) {
            if (entry.valid && (entry.adpdu.talker_capabilities.get() & talker_capabilities::IMPLEMENTED) != 0) {
                fn(entry);
            }
        }
    }

    /// Iterate over entities with listener capabilities
    template <typename F>
    void for_each_listener(F const& fn) const
    {
        for (auto const& entry : entities_) {
            if (entry.valid && (entry.adpdu.listener_capabilities.get() & listener_capabilities::IMPLEMENTED) != 0) {
                fn(entry);
            }
        }
    }

  private:
    std::array<DiscoveredEntity, MaxEntities> entities_{};
};

//
// Discovery State Machine - IEEE 1722.1 Clause 6.2.4
//

/// Discovery state machine states
enum class DiscoveryState : uint8_t
{
    Start,
    Waiting,
    Count
};

/// Discovery state machine events
enum class DiscoveryEvent : uint8_t
{
    UCT,
    RcvdAvailable,
    RcvdDeparting,
    DoDiscover,
    Count
};

/// Discovery state machine context
struct DiscoveryContext
{
    AdpEntityDatabase<64> entities;

    // Received ADPDU being processed
    AdpDu rcvd_adpdu{};
    ieee::Eui48 rcvd_src_mac{};

    // Discover target (all-zero = broadcast)
    ieee::Eui64 discover_target{};

    // Callbacks
    statusbar::sg14::inplace_function<bool(AdpDu const&), 64> tx_discover;
    statusbar::sg14::inplace_function<void(DiscoveredEntity const&), 64> on_entity_available;
    statusbar::sg14::inplace_function<void(DiscoveredEntity const&), 64> on_entity_updated;
    statusbar::sg14::inplace_function<void(ieee::Eui64), 64> on_entity_departing;
};

//
// Discovery Actions
//

namespace discovery_actions {

/// Handle ENTITY_AVAILABLE
void handle_available(DiscoveryContext& ctx, sm::TimePoint event_time);

/// Handle ENTITY_DEPARTING
void handle_departing(DiscoveryContext& ctx, sm::TimePoint event_time);

/// Send ENTITY_DISCOVER
void send_discover(DiscoveryContext& ctx, sm::TimePoint event_time);

}  // namespace discovery_actions

//
// Discovery State Machine Definition
//

struct DiscoveryDef
{
    using Context = DiscoveryContext;
    using State = DiscoveryState;
    using Event = DiscoveryEvent;
};

/// Build the discovery state machine transition table
consteval auto make_discovery_table()
{
    using T = sm::Transitions<DiscoveryDef>;
    using State = DiscoveryState;
    using Event = DiscoveryEvent;

    sm::TransitionTable<DiscoveryDef> t{};

    t.at(State::Start, Event::UCT) = T::transition(State::Waiting);

    t.at(State::Waiting, Event::RcvdAvailable) = T::action<discovery_actions::handle_available>(State::Waiting);
    t.at(State::Waiting, Event::RcvdDeparting) = T::action<discovery_actions::handle_departing>(State::Waiting);
    t.at(State::Waiting, Event::DoDiscover) = T::action<discovery_actions::send_discover>(State::Waiting);

    return t;
}

inline constexpr auto discovery_table = make_discovery_table();

template <typename Observer = sm::NullObserver>
using AdpDiscoveryStateMachine = sm::StateMachine<DiscoveryDef, discovery_table, Observer>;

}  // namespace statusbar::atdecc
