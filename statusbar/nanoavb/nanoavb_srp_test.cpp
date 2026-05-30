// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/ieee/ieee.hpp"
#include "statusbar/nanoavb/nanoavb.hpp"
#include "statusbar/sm/sm.hpp"
#include "statusbar/test/test.hpp"
#include "statusbar/tsn/tsn.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <system_error>

using namespace statusbar::nanoavb;
using statusbar::ieee::Eui48;
using TimePoint = statusbar::sm::TimePoint;

//
// MVRP Constants Tests
//
TEST(nanoavb_srp_mvrp, ethertype)
{
    EXPECT_EQ(MvrpHandler::ethertype(), 0x88F5);
}

TEST(nanoavb_srp_mvrp, multicast_address)
{
    EXPECT_EQ(MvrpHandler::multicast_address(), 0x0180C2000021ULL);
}

//
// MVRP Handler Tests
//
TEST(nanoavb_srp_mvrp, register_vlan)
{
    MvrpHandler handler{statusbar::srp::mvrp::MvrpConfig{}};

    auto result = handler.register_vlan(2, TimePoint{std::chrono::seconds{1}});
    EXPECT_TRUE(result.has_value());

    EXPECT_EQ(handler.get_vlan_state(2), VlanState::Pending);
}

TEST(nanoavb_srp_mvrp, register_invalid_vlan)
{
    MvrpHandler handler{statusbar::srp::mvrp::MvrpConfig{}};

    auto result = handler.register_vlan(0, TimePoint{std::chrono::seconds{1}});
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error() == make_error_code(NanoAvbError::InvalidVlanId));

    result = handler.register_vlan(4095, TimePoint{std::chrono::seconds{1}});
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error() == make_error_code(NanoAvbError::InvalidVlanId));
}

TEST(nanoavb_srp_mvrp, withdraw_vlan)
{
    MvrpHandler handler{statusbar::srp::mvrp::MvrpConfig{}};

    (void)handler.register_vlan(2, TimePoint{std::chrono::seconds{1}});
    EXPECT_EQ(handler.get_vlan_state(2), VlanState::Pending);

    auto result = handler.withdraw_vlan(2, TimePoint{std::chrono::seconds{1}});
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(handler.get_vlan_state(2), VlanState::Unregistered);
}

TEST(nanoavb_srp_mvrp, withdraw_nonexistent_vlan)
{
    MvrpHandler handler{statusbar::srp::mvrp::MvrpConfig{}};

    // Withdrawing a non-existent VLAN should succeed (idempotent)
    auto result = handler.withdraw_vlan(99, TimePoint{std::chrono::seconds{1}});
    EXPECT_TRUE(result.has_value());
}

TEST(nanoavb_srp_mvrp, multiple_vlans)
{
    MvrpHandler handler{statusbar::srp::mvrp::MvrpConfig{}};

    (void)handler.register_vlan(2, TimePoint{std::chrono::seconds{1}});
    (void)handler.register_vlan(100, TimePoint{std::chrono::seconds{1}});
    (void)handler.register_vlan(200, TimePoint{std::chrono::seconds{1}});

    EXPECT_EQ(handler.vlans().size(), 3);
    EXPECT_EQ(handler.get_vlan_state(2), VlanState::Pending);
    EXPECT_EQ(handler.get_vlan_state(100), VlanState::Pending);
    EXPECT_EQ(handler.get_vlan_state(200), VlanState::Pending);
}

TEST(nanoavb_srp_mvrp, receive_short_packet)
{
    MvrpHandler handler{statusbar::srp::mvrp::MvrpConfig{}};

    // Too short packet should be ignored
    std::array<uint8_t, 2> short_packet = {0, 0};
    handler.receive_packet(short_packet, TimePoint{std::chrono::seconds{1}});  // Should not crash
}

//
// MSRP Constants Tests
//
TEST(nanoavb_srp_msrp, ethertype)
{
    EXPECT_EQ(MsrpHandler<>::ethertype(), 0x22EA);
}

TEST(nanoavb_srp_msrp, multicast_address)
{
    EXPECT_EQ(MsrpHandler<>::multicast_address(), 0x0180C200000EULL);
}

//
// MSRP Handler Domain Tests
//
TEST(nanoavb_srp_msrp, default_domain)
{
    MsrpHandler<> handler{statusbar::srp::msrp::MsrpConfig{}};

    auto const& domain = handler.domain();
    EXPECT_EQ(domain.sr_class_id, 6);
    EXPECT_EQ(domain.sr_class_priority, 3);
    EXPECT_EQ(domain.sr_class_vid, 2);
}

TEST(nanoavb_srp_msrp, set_domain)
{
    MsrpHandler<> handler{statusbar::srp::msrp::MsrpConfig{}};

    DomainInfo new_domain{5, 2, 100};
    handler.set_domain(new_domain);

    auto const& domain = handler.domain();
    EXPECT_EQ(domain.sr_class_id, 5);
    EXPECT_EQ(domain.sr_class_priority, 2);
    EXPECT_EQ(domain.sr_class_vid, 100);
}

//
// MSRP Handler Talker Tests
//
TEST(nanoavb_srp_msrp, talker_advertise)
{
    MsrpHandler<> handler{statusbar::srp::msrp::MsrpConfig{}};

    statusbar::tsn::StreamId stream_id;
    stream_id.set_system_address(Eui48{0x00, 0x01, 0x02, 0x03, 0x04, 0x05});
    stream_id.set_unique_id(1);

    TalkerStreamSrpInfo info;
    info.stream_id = stream_id;
    info.max_frame_size = 128;
    info.max_interval_frames = 1;

    auto result = handler.talker_advertise(info, TimePoint{std::chrono::seconds{1}});
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);

    auto const* stored = handler.get_talker_stream(stream_id);
    EXPECT_TRUE(stored != nullptr);
    EXPECT_EQ(stored->state, TalkerReservationState::Advertising);
    EXPECT_EQ(stored->max_frame_size, 128);
}

TEST(nanoavb_srp_msrp, talker_withdraw)
{
    MsrpHandler<> handler{statusbar::srp::msrp::MsrpConfig{}};

    statusbar::tsn::StreamId stream_id;
    stream_id.set_system_address(Eui48{0x00, 0x01, 0x02, 0x03, 0x04, 0x05});
    stream_id.set_unique_id(1);

    TalkerStreamSrpInfo info;
    info.stream_id = stream_id;

    (void)handler.talker_advertise(info, TimePoint{std::chrono::seconds{1}});

    auto result = handler.talker_withdraw(stream_id, TimePoint{std::chrono::seconds{1}});
    EXPECT_TRUE(result.has_value());

    auto const* stored = handler.get_talker_stream(stream_id);
    EXPECT_TRUE(stored != nullptr);
    EXPECT_EQ(stored->state, TalkerReservationState::Idle);
}

TEST(nanoavb_srp_msrp, talker_withdraw_nonexistent)
{
    MsrpHandler<> handler{statusbar::srp::msrp::MsrpConfig{}};

    statusbar::tsn::StreamId stream_id;
    stream_id.set_unique_id(99);

    auto result = handler.talker_withdraw(stream_id, TimePoint{std::chrono::seconds{1}});
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error() == make_error_code(NanoAvbError::InvalidStreamIndex));
}

//
// MSRP Handler Listener Tests
//
TEST(nanoavb_srp_msrp, listener_ready)
{
    MsrpHandler<> handler{statusbar::srp::msrp::MsrpConfig{}};

    statusbar::tsn::StreamId stream_id;
    stream_id.set_system_address(Eui48{0x00, 0x01, 0x02, 0x03, 0x04, 0x05});
    stream_id.set_unique_id(1);

    auto result = handler.listener_ready(stream_id, TimePoint{std::chrono::seconds{1}});
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);

    auto const* stored = handler.get_listener_stream(stream_id);
    EXPECT_TRUE(stored != nullptr);
    EXPECT_EQ(stored->state, ListenerReservationState::Ready);
}

TEST(nanoavb_srp_msrp, listener_asking_failed)
{
    MsrpHandler<> handler{statusbar::srp::msrp::MsrpConfig{}};

    statusbar::tsn::StreamId stream_id;
    stream_id.set_system_address(Eui48{0x00, 0x01, 0x02, 0x03, 0x04, 0x05});
    stream_id.set_unique_id(1);

    auto result = handler.listener_asking_failed(stream_id, TimePoint{std::chrono::seconds{1}});
    EXPECT_TRUE(result.has_value());

    auto const* stored = handler.get_listener_stream(stream_id);
    EXPECT_TRUE(stored != nullptr);
    EXPECT_EQ(stored->state, ListenerReservationState::AskingFailed);
}

TEST(nanoavb_srp_msrp, listener_withdraw)
{
    MsrpHandler<> handler{statusbar::srp::msrp::MsrpConfig{}};

    statusbar::tsn::StreamId stream_id;
    stream_id.set_system_address(Eui48{0x00, 0x01, 0x02, 0x03, 0x04, 0x05});
    stream_id.set_unique_id(1);

    (void)handler.listener_ready(stream_id, TimePoint{std::chrono::seconds{1}});

    auto result = handler.listener_withdraw(stream_id, TimePoint{std::chrono::seconds{1}});
    EXPECT_TRUE(result.has_value());

    auto const* stored = handler.get_listener_stream(stream_id);
    EXPECT_TRUE(stored != nullptr);
    EXPECT_EQ(stored->state, ListenerReservationState::Idle);
}

TEST(nanoavb_srp_msrp, listener_withdraw_nonexistent)
{
    MsrpHandler<> handler{statusbar::srp::msrp::MsrpConfig{}};

    statusbar::tsn::StreamId stream_id;
    stream_id.set_unique_id(99);

    // Withdrawing a non-existent listener should succeed (idempotent)
    auto result = handler.listener_withdraw(stream_id, TimePoint{std::chrono::seconds{1}});
    EXPECT_TRUE(result.has_value());
}

TEST(nanoavb_srp_msrp, multiple_streams)
{
    MsrpHandler<> handler{statusbar::srp::msrp::MsrpConfig{}};

    for (int i = 0; i < 4; ++i) {
        statusbar::tsn::StreamId stream_id;
        stream_id.set_system_address(Eui48{0x00, 0x01, 0x02, 0x03, 0x04, 0x05});
        stream_id.set_unique_id(i);

        TalkerStreamSrpInfo info;
        info.stream_id = stream_id;
        (void)handler.talker_advertise(info, TimePoint{std::chrono::seconds{1}});
    }

    EXPECT_EQ(handler.talker_streams().size(), 4);
}

TEST(nanoavb_srp_msrp, receive_short_packet)
{
    MsrpHandler<> handler{statusbar::srp::msrp::MsrpConfig{}};

    // Too short packet should be ignored
    std::array<uint8_t, 2> short_packet = {0, 0};
    handler.receive_packet(short_packet, TimePoint{std::chrono::seconds{1}});  // Should not crash
}

//
// Test Runner
//
int statusbar_nanoavb_nanoavb_srp_test(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    int result = 0;
    result |= ::statusbar::test::TestRegister::run_section("nanoavb_srp_test");
    return result;
}