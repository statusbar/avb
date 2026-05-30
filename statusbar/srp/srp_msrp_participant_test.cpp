// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/srp/srp_msrp_participant.hpp"

#include "statusbar/ieee/ieee.hpp"
#include "statusbar/srp/srp_msrp_format.hpp"
#include "statusbar/test/test.hpp"
#include "statusbar/tsn/tsn_stream_id.hpp"

#include <chrono>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

using namespace statusbar;
using namespace statusbar::srp::msrp;
using statusbar::srp::mrp::Operation;
using statusbar::tsn::StreamId;
using TimePoint = statusbar::sm::TimePoint;

namespace {

struct Fabric
{
    std::vector<std::vector<uint8_t>> a_to_b{};
    std::vector<std::vector<uint8_t>> b_to_a{};
    [[nodiscard]] auto make_a_sender() -> std::function<bool(std::span<uint8_t const>)>
    {
        return [this](std::span<uint8_t const> pdu) {
            a_to_b.emplace_back(pdu.begin(), pdu.end());
            return true;
        };
    }
    [[nodiscard]] auto make_b_sender() -> std::function<bool(std::span<uint8_t const>)>
    {
        return [this](std::span<uint8_t const> pdu) {
            b_to_a.emplace_back(pdu.begin(), pdu.end());
            return true;
        };
    }
    void deliver_a_to_b(MsrpParticipant& b, TimePoint now)
    {
        for (auto const& pdu : a_to_b) {
            b.receive_pdu(std::span<uint8_t const>(pdu.data(), pdu.size()), now);
        }
        a_to_b.clear();
    }
    void deliver_b_to_a(MsrpParticipant& a, TimePoint now)
    {
        for (auto const& pdu : b_to_a) {
            a.receive_pdu(std::span<uint8_t const>(pdu.data(), pdu.size()), now);
        }
        b_to_a.clear();
    }
};

struct TestClock
{
    TimePoint now{TimePoint{} + std::chrono::seconds(1)};
    auto advance(std::chrono::milliseconds d) -> TimePoint
    {
        now += d;
        return now;
    }
};

auto make_stream_id(uint16_t uid) -> StreamId
{
    ieee::Eui48 const mac{0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    return tsn::StreamId{mac, uid};
}

auto test_msrp_config() -> MsrpConfig
{
    return MsrpConfig{
        .max_talker_advertise = 8,
        .max_talker_failed = 4,
        .max_listeners = 8,
        .max_domains = 2,
        .max_observers = 4,
        .max_interesting_stream_ids = 4,
    };
}

auto make_talker_adv(uint16_t uid) -> TalkerAdvertiseFirstValue
{
    TalkerAdvertiseFirstValue fv{};
    fv.stream_id = make_stream_id(uid);
    fv.destination_address = ieee::Eui48{0x91, 0xe0, 0xf0, 0x00, 0x12, 0x34};
    fv.vlan_identifier = default_sr_class_vid;
    fv.max_frame_size = 150;
    fv.max_interval_frames = 1;
    fv.set_priority(default_sr_class_priority);
    fv.set_rank(RANK_NON_EMERGENCY);
    fv.accumulated_latency = 0;
    return fv;
}

}  // namespace

TEST(msrp_participant, start_arms_timers)
{
    MsrpParticipant p{test_msrp_config(), 0x12345};
    TestClock clock{};
    p.start(clock.now);
    EXPECT_NE(p.next_deadline(), TimePoint::max());
}

TEST(msrp_participant, declare_talker_emits_pdu_peer_notifies)
{
    MsrpParticipant a{test_msrp_config(), 0xaa11};
    MsrpParticipant b{test_msrp_config(), 0xbb22};
    Fabric fab{};
    a.set_send_pdu(fab.make_a_sender());
    b.set_send_pdu(fab.make_b_sender());
    TestClock clock{};
    a.start(clock.now);
    b.start(clock.now);
    std::optional<TalkerAdvertiseFirstValue> b_received_talker{};
    Observer obs{};
    obs.on_talker_advertise = [&](TalkerAdvertiseFirstValue const& fv, Operation) { b_received_talker = fv; };
    b.subscribe(obs);
    auto const ta = make_talker_adv(0x0001);
    auto status = a.declare_talker_advertise(ta, clock.now);
    EXPECT_TRUE(bool(status));
    auto const t1 = clock.advance(std::chrono::milliseconds(150));
    a.tick(t1);
    EXPECT_TRUE(!fab.a_to_b.empty());
    fab.deliver_a_to_b(b, t1);
    EXPECT_TRUE(b_received_talker.has_value());
    if (b_received_talker.has_value()) {
        EXPECT_EQ(b_received_talker->stream_id.get_unique_id(), 0x0001);
    }
    auto const* looked_up = b.find_talker_advertise(make_stream_id(0x0001));
    EXPECT_NE(looked_up, nullptr);
}

TEST(msrp_participant, withdraw_talker_notifies_peer_leave)
{
    MsrpParticipant a{test_msrp_config(), 0xcc33};
    MsrpParticipant b{test_msrp_config(), 0xdd44};
    Fabric fab{};
    a.set_send_pdu(fab.make_a_sender());
    b.set_send_pdu(fab.make_b_sender());
    TestClock clock{};
    a.start(clock.now);
    b.start(clock.now);
    std::optional<tsn::StreamId> leave_id{};
    Observer obs{};
    obs.on_talker_leave = [&](tsn::StreamId const& id) { leave_id = id; };
    b.subscribe(obs);
    (void)a.declare_talker_advertise(make_talker_adv(0x0042), clock.now);
    auto const t1 = clock.advance(std::chrono::milliseconds(150));
    a.tick(t1);
    fab.deliver_a_to_b(b, t1);
    auto const stream_id = make_stream_id(0x0042);
    (void)a.withdraw_talker(stream_id, clock.now);
    auto const t2 = clock.advance(std::chrono::milliseconds(150));
    a.tick(t2);
    fab.deliver_a_to_b(b, t2);
    auto const t3 = clock.advance(std::chrono::milliseconds(1100));
    b.tick(t3);
    EXPECT_TRUE(leave_id.has_value());
    if (leave_id.has_value()) {
        EXPECT_EQ(leave_id->get_unique_id(), 0x0042);
    }
}

TEST(msrp_participant, listener_declare_reaches_peer)
{
    MsrpParticipant a{test_msrp_config(), 0xee55};
    MsrpParticipant b{test_msrp_config(), 0xff66};
    Fabric fab{};
    a.set_send_pdu(fab.make_a_sender());
    b.set_send_pdu(fab.make_b_sender());
    TestClock clock{};
    a.start(clock.now);
    b.start(clock.now);
    std::optional<std::tuple<tsn::StreamId, ListenerDeclaration>> listener_seen{};
    Observer obs{};
    obs.on_listener = [&](tsn::StreamId const& id, ListenerDeclaration decl, Operation) {
        listener_seen = std::make_tuple(id, decl);
    };
    a.subscribe(obs);
    auto const sid = make_stream_id(0x0099);
    (void)b.declare_listener(sid, ListenerDeclaration::Ready, clock.now);
    auto const t1 = clock.advance(std::chrono::milliseconds(150));
    b.tick(t1);
    fab.deliver_b_to_a(a, t1);
    EXPECT_TRUE(listener_seen.has_value());
    if (listener_seen.has_value()) {
        EXPECT_EQ(std::get<0>(*listener_seen).get_unique_id(), 0x0099);
        EXPECT_EQ(static_cast<int>(std::get<1>(*listener_seen)), static_cast<int>(ListenerDeclaration::Ready));
    }
}

TEST(msrp_participant, interesting_stream_id_pruning)
{
    MsrpParticipant a{test_msrp_config(), 0x1111};
    MsrpParticipant b{test_msrp_config(), 0x2222};
    Fabric fab{};
    a.set_send_pdu(fab.make_a_sender());
    b.set_send_pdu(fab.make_b_sender());
    TestClock clock{};
    a.start(clock.now);
    b.start(clock.now);
    b.set_pruning_enabled(true);
    EXPECT_TRUE(is_success(b.add_interesting_stream_id(make_stream_id(0x0001))));
    EXPECT_EQ(b.interesting_stream_id_count(), 1u);
    int talker_advertise_count = 0;
    Observer obs{};
    obs.on_talker_advertise = [&](TalkerAdvertiseFirstValue const&, Operation) { ++talker_advertise_count; };
    b.subscribe(obs);
    (void)a.declare_talker_advertise(make_talker_adv(0x0001), clock.now);
    (void)a.declare_talker_advertise(make_talker_adv(0x0002), clock.now);
    auto const t1 = clock.advance(std::chrono::milliseconds(150));
    a.tick(t1);
    fab.deliver_a_to_b(b, t1);
    EXPECT_EQ(talker_advertise_count, 1);
    EXPECT_EQ(b.talker_advertise_count(), 1u);
}

TEST(msrp_participant, unsubscribe_stops_notifications)
{
    MsrpParticipant p{test_msrp_config(), 0x3333};
    TestClock clock{};
    p.start(clock.now);
    int count = 0;
    Observer obs{};
    obs.on_talker_advertise = [&](auto const&, auto) { ++count; };
    auto const sub = p.subscribe(obs);
    p.unsubscribe(sub);
    EXPECT_EQ(count, 0);
    p.unsubscribe(sub);
}

TEST(msrp_participant, add_interesting_stream_id_capacity)
{
    auto const cfg = test_msrp_config();
    MsrpParticipant p{cfg};
    p.set_pruning_enabled(true);
    for (uint16_t i = 1; i <= 4; ++i) {
        EXPECT_TRUE(is_success(p.add_interesting_stream_id(make_stream_id(i))));
    }
    EXPECT_EQ(p.interesting_stream_id_count(), 4u);
    EXPECT_TRUE(is_failure(p.add_interesting_stream_id(make_stream_id(0x0005))));
    EXPECT_EQ(p.interesting_stream_id_count(), 4u);
    EXPECT_TRUE(is_success(p.add_interesting_stream_id(make_stream_id(0x0001))));
}

//
// Domain declaration + exchange
//

TEST(msrp_participant, declare_domain_basic)
{
    MsrpParticipant a{test_msrp_config(), 0xaa01};
    MsrpParticipant b{test_msrp_config(), 0xbb02};
    Fabric fab{};
    a.set_send_pdu(fab.make_a_sender());
    b.set_send_pdu(fab.make_b_sender());
    TestClock clock{};
    a.start(clock.now);
    b.start(clock.now);
    std::optional<DomainFirstValue> domain_seen{};
    Observer obs{};
    obs.on_domain = [&](DomainFirstValue const& fv, Operation) { domain_seen = fv; };
    b.subscribe(obs);
    DomainFirstValue dfv{};
    dfv.sr_class_id = SR_CLASS_A;
    dfv.sr_class_priority = default_sr_class_priority;
    dfv.sr_class_vid = default_sr_class_vid;
    EXPECT_TRUE(bool(a.declare_domain(dfv, clock.now)));
    EXPECT_EQ(a.domain_count(), 1u);
    auto const t1 = clock.advance(std::chrono::milliseconds(150));
    a.tick(t1);
    fab.deliver_a_to_b(b, t1);
    EXPECT_TRUE(domain_seen.has_value());
}

TEST(msrp_participant, withdraw_domain_unknown_ok)
{
    MsrpParticipant p{test_msrp_config(), 0xdd04};
    TestClock clock{};
    p.start(clock.now);
    EXPECT_TRUE(bool(p.withdraw_domain(99, clock.now)));
}

TEST(msrp_participant, declare_talker_failed_basic)
{
    MsrpParticipant p{test_msrp_config(), 0xee05};
    TestClock clock{};
    p.start(clock.now);
    TalkerFailedFirstValue tfv{};
    tfv.advertise = make_talker_adv(0x0010);
    tfv.set_failure_code(FailureCode::InsufficientBandwidth);
    EXPECT_TRUE(bool(p.declare_talker_failed(tfv, clock.now)));
    EXPECT_EQ(p.talker_failed_count(), 1u);
}

TEST(msrp_participant, withdraw_talker_failed)
{
    MsrpParticipant p{test_msrp_config(), 0x1107};
    TestClock clock{};
    p.start(clock.now);
    TalkerFailedFirstValue tfv{};
    tfv.advertise = make_talker_adv(0x0020);
    (void)p.declare_talker_failed(tfv, clock.now);
    EXPECT_TRUE(bool(p.withdraw_talker(make_stream_id(0x0020), clock.now)));
}

TEST(msrp_participant, withdraw_listener_basic)
{
    MsrpParticipant p{test_msrp_config(), 0x2208};
    TestClock clock{};
    p.start(clock.now);
    (void)p.declare_listener(make_stream_id(0x0030), ListenerDeclaration::Ready, clock.now);
    EXPECT_EQ(p.listener_count(), 1u);
    EXPECT_TRUE(bool(p.withdraw_listener(make_stream_id(0x0030), clock.now)));
}

TEST(msrp_participant, withdraw_listener_unknown_ok)
{
    MsrpParticipant p{test_msrp_config(), 0x3309};
    TestClock clock{};
    p.start(clock.now);
    EXPECT_TRUE(bool(p.withdraw_listener(make_stream_id(0xFFFF), clock.now)));
}

TEST(msrp_participant, find_talker_advertise_not_found)
{
    MsrpParticipant p{test_msrp_config(), 0x440a};
    EXPECT_EQ(p.find_talker_advertise(make_stream_id(1)), nullptr);
}
TEST(msrp_participant, find_talker_failed_not_found)
{
    MsrpParticipant p{test_msrp_config(), 0x550b};
    EXPECT_EQ(p.find_talker_failed(make_stream_id(1)), nullptr);
}
TEST(msrp_participant, find_listener_not_found)
{
    MsrpParticipant p{test_msrp_config(), 0x660c};
    EXPECT_EQ(p.find_listener(make_stream_id(1)), nullptr);
}
TEST(msrp_participant, find_domain_not_found)
{
    MsrpParticipant p{test_msrp_config(), 0x770d};
    EXPECT_EQ(p.find_domain(99), nullptr);
}

TEST(msrp_participant, find_talker_advertise_found)
{
    MsrpParticipant p{test_msrp_config(), 0x880e};
    TestClock clock{};
    p.start(clock.now);
    (void)p.declare_talker_advertise(make_talker_adv(0x0042), clock.now);
    EXPECT_NE(p.find_talker_advertise(make_stream_id(0x0042)), nullptr);
}

TEST(msrp_participant, find_domain_found)
{
    MsrpParticipant p{test_msrp_config(), 0x990f};
    TestClock clock{};
    p.start(clock.now);
    DomainFirstValue dfv{};
    dfv.sr_class_id = SR_CLASS_B;
    (void)p.declare_domain(dfv, clock.now);
    EXPECT_NE(p.find_domain(SR_CLASS_B), nullptr);
}

TEST(msrp_participant, listener_permits_no_listener)
{
    MsrpParticipant p{test_msrp_config(), 0xaa10};
    EXPECT_FALSE(p.listener_permits_transmit(make_stream_id(1)));
}

TEST(msrp_participant, listener_permits_local_false)
{
    MsrpParticipant p{test_msrp_config(), 0xbb11};
    TestClock clock{};
    p.start(clock.now);
    (void)p.declare_listener(make_stream_id(1), ListenerDeclaration::Ready, clock.now);
    EXPECT_FALSE(p.listener_permits_transmit(make_stream_id(1)));
}

TEST(msrp_participant, remove_interesting_stream_id)
{
    MsrpParticipant p{test_msrp_config(), 0xcc12};
    p.set_pruning_enabled(true);
    (void)p.add_interesting_stream_id(make_stream_id(1));
    (void)p.add_interesting_stream_id(make_stream_id(2));
    EXPECT_EQ(p.interesting_stream_id_count(), 2u);
    p.remove_interesting_stream_id(make_stream_id(1));
    EXPECT_EQ(p.interesting_stream_id_count(), 1u);
    p.remove_interesting_stream_id(make_stream_id(99));
    EXPECT_EQ(p.interesting_stream_id_count(), 1u);
}

TEST(msrp_participant, clear_interesting_stream_ids)
{
    MsrpParticipant p{test_msrp_config(), 0xdd13};
    p.set_pruning_enabled(true);
    (void)p.add_interesting_stream_id(make_stream_id(1));
    (void)p.add_interesting_stream_id(make_stream_id(2));
    p.clear_interesting_stream_ids();
    EXPECT_EQ(p.interesting_stream_id_count(), 0u);
}

TEST(msrp_participant, pruning_default_disabled)
{
    MsrpParticipant p{test_msrp_config(), 0xee14};
    EXPECT_FALSE(p.pruning_enabled());
}

TEST(msrp_participant, set_pruning_toggle)
{
    MsrpParticipant p{test_msrp_config(), 0xff15};
    p.set_pruning_enabled(true);
    EXPECT_TRUE(p.pruning_enabled());
    p.set_pruning_enabled(false);
    EXPECT_FALSE(p.pruning_enabled());
}

TEST(msrp_participant, invalid_config_throws)
{
    MsrpConfig bad{};
    bad.max_interesting_stream_ids = 100;
    bool threw = false;
    try {
        MsrpParticipant p{bad};
    } catch (std::system_error const&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(msrp_participant, initial_counts_zero)
{
    MsrpParticipant p{test_msrp_config(), 0x1116};
    EXPECT_EQ(p.talker_advertise_count(), 0u);
    EXPECT_EQ(p.talker_failed_count(), 0u);
    EXPECT_EQ(p.listener_count(), 0u);
    EXPECT_EQ(p.domain_count(), 0u);
}

TEST(msrp_participant, talker_adv_capacity_limit)
{
    MsrpConfig cfg{};
    cfg.max_talker_advertise = 2;
    cfg.max_interesting_stream_ids = 2;
    MsrpParticipant p{cfg, 0x2217};
    TestClock clock{};
    p.start(clock.now);
    EXPECT_TRUE(is_success(p.declare_talker_advertise(make_talker_adv(1), clock.now)));
    EXPECT_TRUE(is_success(p.declare_talker_advertise(make_talker_adv(2), clock.now)));
    EXPECT_TRUE(is_failure(p.declare_talker_advertise(make_talker_adv(3), clock.now)));
}

TEST(msrp_participant, stop_preserves_attrs)
{
    MsrpParticipant p{test_msrp_config(), 0x3318};
    TestClock clock{};
    p.start(clock.now);
    (void)p.declare_talker_advertise(make_talker_adv(1), clock.now);
    p.stop();
    EXPECT_EQ(p.talker_advertise_count(), 1u);
}

TEST(msrp_participant, receive_pdu_too_short)
{
    MsrpParticipant p{test_msrp_config(), 0x4419};
    TestClock clock{};
    p.start(clock.now);
    std::array<uint8_t, 2> buf{0x00, 0x00};
    p.receive_pdu(buf, clock.now);
    EXPECT_EQ(p.talker_advertise_count(), 0u);
}

TEST(msrp_participant, receive_pdu_wrong_version)
{
    MsrpParticipant p{test_msrp_config(), 0x551a};
    TestClock clock{};
    p.start(clock.now);
    std::array<uint8_t, 3> buf{0x01, 0x00, 0x00};
    p.receive_pdu(buf, clock.now);
    EXPECT_EQ(p.talker_advertise_count(), 0u);
}

TEST(msrp_config_validate, valid_default)
{
    MsrpConfig cfg{};
    EXPECT_TRUE(is_success(cfg.validate()));
}

TEST(msrp_config_validate, invalid_exceeds)
{
    MsrpConfig cfg{};
    cfg.max_interesting_stream_ids = cfg.max_talker_advertise + 1;
    EXPECT_TRUE(is_failure(cfg.validate()));
}

TEST(msrp_participant, leaveall_emits_pdu)
{
    MsrpParticipant a{test_msrp_config(), 0x661b};
    Fabric fab{};
    a.set_send_pdu(fab.make_a_sender());
    TestClock clock{};
    a.start(clock.now);
    (void)a.declare_talker_advertise(make_talker_adv(1), clock.now);
    auto t1 = clock.advance(std::chrono::milliseconds(150));
    a.tick(t1);
    fab.a_to_b.clear();
    auto t2 = clock.advance(std::chrono::seconds(20));
    a.tick(t2);
    EXPECT_TRUE(!fab.a_to_b.empty());
}

//
// Pretty-print helpers (tsn_msrp_format.hpp, tsn_mrp_format.hpp).
// Capture a real MSRPDU, feed it through format_msrp, and verify
// non-empty output. Exercises format_msrp, format_msrp_vector,
// format_first_value, format_stream_id, and format_mrp_events.
//

TEST(msrp_print, format_msrp_of_declare_talker_advertise_pdu)
{
    MsrpParticipant a{test_msrp_config(), 0x1122};
    Fabric fab{};
    a.set_send_pdu(fab.make_a_sender());
    TestClock clock{};
    a.start(clock.now);

    auto const ta = make_talker_adv(0x0001);
    auto status = a.declare_talker_advertise(ta, clock.now);
    EXPECT_TRUE(bool(status));
    auto const t1 = clock.advance(std::chrono::milliseconds(150));
    a.tick(t1);
    EXPECT_TRUE(!fab.a_to_b.empty());

    std::string out;
    (void)statusbar::srp::msrp::format_msrp(std::back_inserter(out), std::span<uint8_t const>(fab.a_to_b.front()));
    EXPECT_TRUE(!out.empty());
}

TEST(msrp_print, format_msrp_of_listener_and_domain_pdus)
{
    MsrpParticipant a{test_msrp_config(), 0x9988};
    Fabric fab{};
    a.set_send_pdu(fab.make_a_sender());
    TestClock clock{};
    a.start(clock.now);

    auto const listener_status = a.declare_listener(make_stream_id(0x0042), ListenerDeclaration::Ready, clock.now);
    EXPECT_TRUE(bool(listener_status));

    DomainFirstValue domain{};
    domain.sr_class_id = 6;
    domain.sr_class_priority = 3;
    domain.sr_class_vid.set(2);
    auto const domain_status = a.declare_domain(domain, clock.now);
    EXPECT_TRUE(bool(domain_status));

    auto const t1 = clock.advance(std::chrono::milliseconds(150));
    a.tick(t1);
    EXPECT_TRUE(!fab.a_to_b.empty());

    std::string out;
    for (auto const& pdu : fab.a_to_b) {
        (void)statusbar::srp::msrp::format_msrp(std::back_inserter(out), std::span<uint8_t const>(pdu));
    }
    EXPECT_TRUE(!out.empty());
}

//
// Malformed-PDU decoder safety: every "return" path in receive_pdu /
// decode_attribute_list / decode_vector_attribute should be reachable
// without crashing or polluting state. The participant must not declare
// any new records when fed garbage.
//

namespace {

void expect_pdu_ignored(MsrpParticipant& p, std::span<uint8_t const> pdu, TestClock& clock)
{
    auto const before = p.talker_advertise_count() + p.listener_count() + p.domain_count() + p.talker_failed_count();
    p.receive_pdu(pdu, clock.now);
    auto const after = p.talker_advertise_count() + p.listener_count() + p.domain_count() + p.talker_failed_count();
    EXPECT_EQ(before, after);
}

}  // namespace

TEST(msrp_participant_decode, empty_pdu)
{
    MsrpParticipant p{test_msrp_config(), 0xa001};
    TestClock clock{};
    p.start(clock.now);
    expect_pdu_ignored(p, std::span<uint8_t const>{}, clock);
}

TEST(msrp_participant_decode, version_only_no_endmark)
{
    MsrpParticipant p{test_msrp_config(), 0xa002};
    TestClock clock{};
    p.start(clock.now);
    std::array<uint8_t, 1> pdu{0x00};
    expect_pdu_ignored(p, pdu, clock);
}

TEST(msrp_participant_decode, attr_header_truncated_no_listlen)
{
    // Version + AttributeType + AttributeLength but no AttributeListLength
    MsrpParticipant p{test_msrp_config(), 0xa003};
    TestClock clock{};
    p.start(clock.now);
    std::array<uint8_t, 3> pdu{0x00, 0x01, 0x19};
    expect_pdu_ignored(p, pdu, clock);
}

TEST(msrp_participant_decode, attr_list_length_extends_past_pdu)
{
    // Talker attr, AttrListLength = 200 (way beyond pdu size)
    MsrpParticipant p{test_msrp_config(), 0xa004};
    TestClock clock{};
    p.start(clock.now);
    std::array<uint8_t, 5> pdu{0x00, 0x01, 0x19, 0x00, 0xC8};
    expect_pdu_ignored(p, pdu, clock);
}

TEST(msrp_participant_decode, attr_list_length_zero_then_endmark)
{
    // Empty attribute list immediately followed by message endmark + final endmark.
    // Should be a no-op, not a crash.
    MsrpParticipant p{test_msrp_config(), 0xa005};
    TestClock clock{};
    p.start(clock.now);
    std::array<uint8_t, 7> pdu{0x00, 0x01, 0x19, 0x00, 0x00, 0x00, 0x00};
    expect_pdu_ignored(p, pdu, clock);
}

TEST(msrp_participant_decode, vector_num_values_overruns_payload)
{
    // Domain attr, AttrListLength = 9 (room for one vector + endmark),
    // but VectorHeader claims 50 values which is larger than payload.
    // Decoder must walk away cleanly.
    MsrpParticipant p{test_msrp_config(), 0xa006};
    TestClock clock{};
    p.start(clock.now);
    std::array<uint8_t, 14> pdu{
        0x00,  // version
        0x04,
        0x04,
        0x00,
        0x09,  // Domain, len=4, list_len=9
        0x00,
        0x32,  // VectorHeader: LA=0, N=50  (lies)
        0x06,
        0x03,
        0x00,
        0x02,  // FirstValue (4 bytes)
        0x00,  // ThreePackedEvents (1 byte; should be ceil(50/3)=17)
        0x00,
        0x00,  // EndMark
    };
    expect_pdu_ignored(p, pdu, clock);
}

TEST(msrp_participant_decode, missing_endmark_within_attr_list)
{
    // AttrListLength = 4, but the inner bytes don't form a valid vector
    // and don't reach EndMark. Decoder must bound the read by attr_list_end.
    MsrpParticipant p{test_msrp_config(), 0xa007};
    TestClock clock{};
    p.start(clock.now);
    std::array<uint8_t, 9> pdu{
        0x00,  // version
        0x04,
        0x04,
        0x00,
        0x04,  // Domain, len=4, list_len=4
        0x00,
        0x01,
        0xFF,
        0xFF,  // partial vector header + garbage
    };
    expect_pdu_ignored(p, pdu, clock);
}

TEST(msrp_participant_decode, wrong_protocol_version_skipped)
{
    // Version 1 (only 0 is valid); decoder must not parse anything.
    MsrpParticipant p{test_msrp_config(), 0xa008};
    TestClock clock{};
    p.start(clock.now);
    std::array<uint8_t, 5> pdu{0x01, 0x04, 0x04, 0x00, 0x09};
    expect_pdu_ignored(p, pdu, clock);
}

TEST(msrp_participant_decode, unknown_attr_type_skipped)
{
    // AttributeType = 7 (undefined). decode_attribute_list switch has no
    // case for it, so the inner loop should skip-past via attr_list_length.
    MsrpParticipant p{test_msrp_config(), 0xa009};
    TestClock clock{};
    p.start(clock.now);
    std::array<uint8_t, 11> pdu{
        0x00,  // version
        0x07,
        0x04,
        0x00,
        0x04,  // unknown attr_type=7, len=4, list_len=4
        0x00,
        0x00,
        0x00,
        0x00,  // 4 bytes of inner content
        0x00,
        0x00,  // final EndMark
    };
    expect_pdu_ignored(p, pdu, clock);
}

TEST(msrp_participant_decode, talker_advertise_attr_length_too_small)
{
    // Real attr type but attr_length=4 (< sizeof TalkerAdvertiseFirstValue=25).
    // handle_rx_event should bail without dispatching.
    MsrpParticipant p{test_msrp_config(), 0xa00a};
    TestClock clock{};
    p.start(clock.now);
    (void)p.add_interesting_stream_id(make_stream_id(1));
    std::array<uint8_t, 14> pdu{
        0x00,
        0x01,
        0x04,
        0x00,
        0x09,  // TalkerAdvertise, len=4 (lie), list_len=9
        0x00,
        0x01,  // VectorHeader: 1 value
        0xaa,
        0xbb,
        0xcc,
        0xdd,  // 4-byte "first value" (too short for talker)
        0x24,  // ThreePackedEvents (JoinIn)
        0x00,
        0x00,  // EndMark
    };
    expect_pdu_ignored(p, pdu, clock);
}

TEST(msrp_participant_decode, threepacked_event_out_of_range_swallowed)
{
    // ThreePackedEvents byte = 250 -> unpack3 returns first=6 which is
    // outside AttributeEvent enum. handle_rx_event must classify it as
    // unknown and do nothing.
    MsrpParticipant p{test_msrp_config(), 0xa00c};
    TestClock clock{};
    p.start(clock.now);
    (void)p.add_interesting_stream_id(make_stream_id(0xbeef));
    std::array<uint8_t, 37> pdu{
        0x00, 0x01, 0x19, 0x00, 0x1e,                    // TalkerAdvertise, len=25, list_len=30
        0x00, 0x01,                                      // VectorHeader: 1 value
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0xbe, 0xef,  // tsn::StreamId (8)
        0x91, 0xe0, 0xf0, 0x00, 0x12, 0x34,              // dest MAC (6)
        0x00, 0x02,                                      // VLAN (2)
        0x00, 0x96,                                      // max_frame_size (2)
        0x00, 0x01,                                      // max_interval (2)
        0x60,                                            // priority_and_rank (1)
        0x00, 0x00, 0x00, 0x00,                          // accumulated_latency (4) -> total 25
        0xfa,                                            // ThreePackedEvents = 250 (out of range)
        0x00, 0x00,                                      // EndMark
    };
    expect_pdu_ignored(p, pdu, clock);
}

//
// Resource-exhaustion / boundary tests (Batch 2)
//

TEST(msrp_participant, listener_capacity_limit)
{
    MsrpConfig cfg{};
    cfg.max_listeners = 2;
    cfg.max_interesting_stream_ids = 4;
    MsrpParticipant p{cfg, 0xb001};
    TestClock clock{};
    p.start(clock.now);
    EXPECT_TRUE(is_success(p.declare_listener(make_stream_id(1), ListenerDeclaration::Ready, clock.now)));
    EXPECT_TRUE(is_success(p.declare_listener(make_stream_id(2), ListenerDeclaration::Ready, clock.now)));
    EXPECT_TRUE(is_failure(p.declare_listener(make_stream_id(3), ListenerDeclaration::Ready, clock.now)));
}

TEST(msrp_participant, domain_capacity_limit)
{
    MsrpConfig cfg{};
    cfg.max_domains = 1;
    cfg.max_interesting_stream_ids = 4;
    MsrpParticipant p{cfg, 0xb002};
    TestClock clock{};
    p.start(clock.now);
    DomainFirstValue d1{};
    d1.sr_class_id = 6;
    d1.sr_class_priority = 3;
    d1.sr_class_vid.set(2);
    DomainFirstValue d2{};
    d2.sr_class_id = 5;
    d2.sr_class_priority = 2;
    d2.sr_class_vid.set(2);
    EXPECT_TRUE(is_success(p.declare_domain(d1, clock.now)));
    EXPECT_TRUE(is_failure(p.declare_domain(d2, clock.now)));
}

TEST(msrp_participant, declare_redeclare_same_stream_id_idempotent)
{
    // Re-declaring the same stream should not consume a second slot.
    MsrpConfig cfg{};
    cfg.max_talker_advertise = 1;
    cfg.max_interesting_stream_ids = 1;
    MsrpParticipant p{cfg, 0xb004};
    TestClock clock{};
    p.start(clock.now);
    EXPECT_TRUE(is_success(p.declare_talker_advertise(make_talker_adv(1), clock.now)));
    EXPECT_TRUE(is_success(p.declare_talker_advertise(make_talker_adv(1), clock.now)));
    EXPECT_EQ(p.talker_advertise_count(), 1u);
}

TEST_MAIN(statusbar_srp, srp_msrp_participant_test)
