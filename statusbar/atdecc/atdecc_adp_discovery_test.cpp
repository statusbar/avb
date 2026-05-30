// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for ADP Discovery State Machine and Entity Database

#include "statusbar/atdecc/atdecc_adp_discovery.hpp"

#include "statusbar/test/test.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

using namespace statusbar;
using namespace statusbar::atdecc;
using namespace statusbar::ieee;

namespace {

auto make_adp(uint8_t id_byte, uint8_t valid_time_2s = 31, uint32_t avail_idx = 1) -> AdpDu
{
    AdpDu adp{};
    adp.init_entity_available(Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, id_byte}, valid_time_2s);
    adp.available_index = avail_idx;
    adp.entity_capabilities = entity_capabilities::AEM_SUPPORTED;
    return adp;
}

auto make_talker_adp(uint8_t id_byte) -> AdpDu
{
    auto adp = make_adp(id_byte);
    adp.talker_capabilities = talker_capabilities::IMPLEMENTED | talker_capabilities::AUDIO_SOURCE;
    return adp;
}

auto make_listener_adp(uint8_t id_byte) -> AdpDu
{
    auto adp = make_adp(id_byte);
    adp.listener_capabilities = listener_capabilities::IMPLEMENTED | listener_capabilities::AUDIO_SINK;
    return adp;
}

Eui48 const MAC_A{0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
Eui48 const MAC_B{0x00, 0x11, 0x22, 0x33, 0x44, 0x66};

auto test_time(int ms) -> sm::TimePoint
{
    return sm::TimePoint{} + std::chrono::milliseconds(ms);
}

}  // namespace

//
// AdpEntityDatabase Tests
//

TEST(adp_entity_db, add_and_find)
{
    AdpEntityDatabase<8> db;
    auto adp = make_adp(0x01);

    auto result = db.process_available(adp, MAC_A, test_time(0));
    EXPECT_EQ(static_cast<int>(result), static_cast<int>(AdpProcessResult::Added));
    EXPECT_EQ(db.count(), 1U);

    auto const* entity = db.find(adp.entity_id);
    EXPECT_TRUE(entity != nullptr);
    EXPECT_TRUE(entity->source_mac == MAC_A);
    EXPECT_EQ(entity->last_available_index, 1U);
}

TEST(adp_entity_db, update_on_index_change)
{
    AdpEntityDatabase<8> db;
    auto adp = make_adp(0x01, 31, 1);
    db.process_available(adp, MAC_A, test_time(0));

    // Same entity, different available_index
    adp.available_index = 2;
    auto result = db.process_available(adp, MAC_A, test_time(100));
    EXPECT_EQ(static_cast<int>(result), static_cast<int>(AdpProcessResult::Updated));
    EXPECT_EQ(db.count(), 1U);
}

TEST(adp_entity_db, unchanged_on_same_index)
{
    AdpEntityDatabase<8> db;
    auto adp = make_adp(0x01, 31, 5);
    db.process_available(adp, MAC_A, test_time(0));

    auto result = db.process_available(adp, MAC_A, test_time(100));
    EXPECT_EQ(static_cast<int>(result), static_cast<int>(AdpProcessResult::Unchanged));
}

TEST(adp_entity_db, departing_removes)
{
    AdpEntityDatabase<8> db;
    auto adp = make_adp(0x01);
    db.process_available(adp, MAC_A, test_time(0));
    EXPECT_EQ(db.count(), 1U);

    EXPECT_TRUE(db.process_departing(adp.entity_id));
    EXPECT_EQ(db.count(), 0U);
    EXPECT_TRUE(db.find(adp.entity_id) == nullptr);
}

TEST(adp_entity_db, departing_unknown_returns_false)
{
    AdpEntityDatabase<8> db;
    EXPECT_FALSE(db.process_departing(Eui64{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
}

TEST(adp_entity_db, expire_stale)
{
    AdpEntityDatabase<8> db;
    auto adp = make_adp(0x01, 1);  // valid_time=1 -> 2 seconds
    db.process_available(adp, MAC_A, test_time(0));
    EXPECT_EQ(db.count(), 1U);

    // Not expired yet at 1.9s
    std::vector<Eui64> expired;
    db.expire_stale(test_time(1900), [&](DiscoveredEntity const& e) { expired.push_back(e.adpdu.entity_id); });
    EXPECT_EQ(expired.size(), 0U);
    EXPECT_EQ(db.count(), 1U);

    // Expired at 2.1s
    db.expire_stale(test_time(2100), [&](DiscoveredEntity const& e) { expired.push_back(e.adpdu.entity_id); });
    EXPECT_EQ(expired.size(), 1U);
    EXPECT_EQ(db.count(), 0U);
}

TEST(adp_entity_db, mac_for_entity)
{
    AdpEntityDatabase<8> db;
    auto adp = make_adp(0x01);
    db.process_available(adp, MAC_A, test_time(0));

    auto mac = db.mac_for_entity(adp.entity_id);
    EXPECT_TRUE(mac.has_value());
    EXPECT_TRUE(*mac == MAC_A);

    auto unknown = db.mac_for_entity(Eui64{});
    EXPECT_FALSE(unknown.has_value());
}

TEST(adp_entity_db, capacity_eviction)
{
    AdpEntityDatabase<4> db;

    // Fill to capacity
    for (uint8_t i = 1; i <= 4; ++i) {
        auto adp = make_adp(i);
        db.process_available(adp, MAC_A, test_time(i * 100));
    }
    EXPECT_EQ(db.count(), 4U);

    // Add 5th entity - oldest (id=1, time=100) should be evicted
    auto adp5 = make_adp(5);
    auto result = db.process_available(adp5, MAC_B, test_time(600));
    EXPECT_EQ(static_cast<int>(result), static_cast<int>(AdpProcessResult::Full));
    EXPECT_EQ(db.count(), 4U);

    // Entity 1 should be gone, entity 5 should be present
    EXPECT_TRUE(db.find(Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x01}) == nullptr);
    EXPECT_TRUE(db.find(Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x05}) != nullptr);
}

TEST(adp_entity_db, for_each_talker)
{
    AdpEntityDatabase<8> db;
    db.process_available(make_talker_adp(0x01), MAC_A, test_time(0));
    db.process_available(make_listener_adp(0x02), MAC_A, test_time(0));
    db.process_available(make_talker_adp(0x03), MAC_A, test_time(0));

    size_t talker_count = 0;
    db.for_each_talker([&](DiscoveredEntity const&) { ++talker_count; });
    EXPECT_EQ(talker_count, 2U);
}

TEST(adp_entity_db, for_each_listener)
{
    AdpEntityDatabase<8> db;
    db.process_available(make_talker_adp(0x01), MAC_A, test_time(0));
    db.process_available(make_listener_adp(0x02), MAC_A, test_time(0));

    size_t listener_count = 0;
    db.for_each_listener([&](DiscoveredEntity const&) { ++listener_count; });
    EXPECT_EQ(listener_count, 1U);
}

//
// Discovery State Machine Tests
//

TEST(adp_discovery_sm, initial_state)
{
    AdpDiscoveryStateMachine<> sm;
    EXPECT_EQ(sm.current_state(), DiscoveryState::Start);
}

TEST(adp_discovery_sm, available_adds_entity)
{
    DiscoveryContext ctx;
    std::vector<Eui64> available_entities;
    ctx.on_entity_available = [&](DiscoveredEntity const& e) { available_entities.push_back(e.adpdu.entity_id); };

    AdpDiscoveryStateMachine<> sm;
    sm.handle_event(ctx, DiscoveryEvent::UCT, test_time(0));
    EXPECT_EQ(sm.current_state(), DiscoveryState::Waiting);

    ctx.rcvd_adpdu = make_adp(0x01);
    ctx.rcvd_src_mac = MAC_A;
    sm.handle_event(ctx, DiscoveryEvent::RcvdAvailable, test_time(0));

    EXPECT_EQ(available_entities.size(), 1U);
    EXPECT_EQ(ctx.entities.count(), 1U);
}

TEST(adp_discovery_sm, available_updated_callback)
{
    DiscoveryContext ctx;
    std::vector<Eui64> updated_entities;
    ctx.on_entity_available = [](DiscoveredEntity const&) {};
    ctx.on_entity_updated = [&](DiscoveredEntity const& e) { updated_entities.push_back(e.adpdu.entity_id); };

    AdpDiscoveryStateMachine<> sm;
    sm.handle_event(ctx, DiscoveryEvent::UCT, test_time(0));

    ctx.rcvd_adpdu = make_adp(0x01, 31, 1);
    ctx.rcvd_src_mac = MAC_A;
    sm.handle_event(ctx, DiscoveryEvent::RcvdAvailable, test_time(0));

    ctx.rcvd_adpdu = make_adp(0x01, 31, 2);  // Changed available_index
    sm.handle_event(ctx, DiscoveryEvent::RcvdAvailable, test_time(100));

    EXPECT_EQ(updated_entities.size(), 1U);
}

TEST(adp_discovery_sm, departing_removes_entity)
{
    DiscoveryContext ctx;
    std::vector<Eui64> departed;
    ctx.on_entity_available = [](DiscoveredEntity const&) {};
    ctx.on_entity_departing = [&](Eui64 id) { departed.push_back(id); };

    AdpDiscoveryStateMachine<> sm;
    sm.handle_event(ctx, DiscoveryEvent::UCT, test_time(0));

    // Add entity
    ctx.rcvd_adpdu = make_adp(0x01);
    ctx.rcvd_src_mac = MAC_A;
    sm.handle_event(ctx, DiscoveryEvent::RcvdAvailable, test_time(0));
    EXPECT_EQ(ctx.entities.count(), 1U);

    // Depart
    AdpDu departing{};
    departing.init_entity_departing(Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x01});
    ctx.rcvd_adpdu = departing;
    sm.handle_event(ctx, DiscoveryEvent::RcvdDeparting, test_time(100));

    EXPECT_EQ(ctx.entities.count(), 0U);
    EXPECT_EQ(departed.size(), 1U);
}

TEST(adp_discovery_sm, send_discover)
{
    DiscoveryContext ctx;
    std::vector<AdpDu> sent;
    ctx.tx_discover = [&](AdpDu const& adp) {
        sent.push_back(adp);
        return true;
    };

    AdpDiscoveryStateMachine<> sm;
    sm.handle_event(ctx, DiscoveryEvent::UCT, test_time(0));

    ctx.discover_target = {};  // Broadcast
    sm.handle_event(ctx, DiscoveryEvent::DoDiscover, test_time(0));

    EXPECT_EQ(sent.size(), 1U);
    EXPECT_TRUE(sent[0].is_entity_discover());
}

//
// Test Runner
//

TEST_MAIN(statusbar_atdecc, atdecc_adp_discovery_test)
