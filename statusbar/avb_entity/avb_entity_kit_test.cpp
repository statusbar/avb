// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// The kit's talker streams and the SR class domain: ACMP's picture of each
// stream and the MSRP Talker Advertise carry the VLAN and priority the
// frames are tagged with, configured or adopted from the wire (Milan v1.3
// 4.2.7.2.1), never the headers' defaults.

#include "statusbar/avb_entity/avb_entity_kit.hpp"

#include "statusbar/atdecc/atdecc_descriptor_storage.hpp"
#include "statusbar/avb_entity/avb_entity_audio_io_config.hpp"
#include "statusbar/avb_entity/avb_entity_descriptor_helpers.hpp"
#include "statusbar/avb_entity/avb_entity_stream_spec.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/itc/itc_stop_token.hpp"
#include "statusbar/nanoavb/nanoavb_srp.hpp"
#include "statusbar/net/net_message_reactor.hpp"
#include "statusbar/ptpclient/ptpclient_media_clock.hpp"
#include "statusbar/sm/sm.hpp"
#include "statusbar/test/test.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace statusbar;
using namespace statusbar::avb_entity;
using statusbar::ieee::Eui48;
using statusbar::ieee::Eui64;
using statusbar::nanoavb::DomainInfo;
using TimePoint = statusbar::sm::TimePoint;

namespace {

auto load_file(std::filesystem::path const& path) -> std::vector<uint8_t>
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Talker-only descriptor blob (committed fixture; source: examples/tone.json).
auto entity_tone_bin_path() -> std::filesystem::path
{
    return std::filesystem::path{__FILE__}.parent_path() / "testdata" / "entity_tone.bin";
}

/// The reactor's clock for a kit started in a test: deterministic, 1 ms a read.
struct FakeClock
{
    std::atomic<int64_t>* now_ns;
    auto operator()() noexcept -> int64_t { return now_ns->fetch_add(1'000'000LL); }
};

/// A kit over the tone blob, the way the tone generator builds its own, with
/// the collaborators the kit binds declared before it. start() brings up its
/// control plane on an interface that does not exist: every socket fails to
/// open (the handlers say so and carry on), so no frame leaves the test, and
/// the protocol handlers are fed by hand.
class KitUnderTest
{
  public:
    explicit KitUnderTest(AvbEntityAudioIOConfig config)
        : config_{std::move(config)}
        , storage_{*atdecc::aem::DescriptorStorage::create(std::span<uint8_t const>{config_.descriptor_storage_blob})}
        , handler_{storage_, config_.entity_id, config_.entity_model_id, "1.0", "kit test", std::nullopt, true}
        , talker_specs_{*talker_stream_specs(storage_, 0)}
        , listener_specs_{*listener_stream_specs(storage_, 0)}
        , kit_{
              config_,
              handler_,
              talker_specs_,
              listener_specs_,
              48000,
              0,
              std::nullopt,
              media_clock_,
              last_gptp_ns_,
              nullptr,
              std::pmr::get_default_resource()}
    {}

    auto operator->() noexcept -> AvbEntityKit* { return &kit_; }
    [[nodiscard]] auto talker_specs() const noexcept -> StreamSpecs const& { return talker_specs_; }

    /// Bring the control plane up (or up again), as the tool does at @p now.
    [[nodiscard]] auto start(TimePoint const now) -> bool { return kit_.start(reactor_, now).has_value(); }
    [[nodiscard]] auto stop(TimePoint const now) -> bool { return kit_.stop(now).has_value(); }

  private:
    AvbEntityAudioIOConfig config_;
    itc::StopToken stop_{};
    std::atomic<int64_t> now_ns_{1};
    net::MessageReactor reactor_{stop_, FakeClock{&now_ns_}, 5};
    atdecc::aem::DescriptorStorage storage_;
    EntityIdentityDescriptorHandler handler_;
    StreamSpecs talker_specs_;
    StreamSpecs listener_specs_;
    ptpclient::MediaClockGenerator media_clock_;
    std::atomic<int64_t> last_gptp_ns_{0};
    AvbEntityKit kit_;
};

auto make_config(uint16_t const vlan_id, std::string_view const profile) -> AvbEntityAudioIOConfig
{
    AvbEntityAudioIOConfig config{};
    config.entity_id = Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    config.entity_model_id = Eui64{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    config.descriptor_storage_blob = load_file(entity_tone_bin_path());
    config.vlan_id = vlan_id;
    config.atdecc_profile = std::string{profile};
    config.interface_name = "no-such-interface-0";
    return config;
}

/// What the kit says the Class A VLAN is, from every place it is said:
/// the SRP handler's Domain, the frames, ACMP's picture of each stream.
void expect_kit_on_vlan(KitUnderTest& kit, uint16_t const vlan_id, uint8_t const priority)
{
    EXPECT_EQ(kit->host().components().msrp_handler.domain().sr_class_vid, vlan_id);
    EXPECT_EQ(kit->host().components().msrp_handler.domain().sr_class_priority, priority);
    EXPECT_EQ(kit->talker().vlan_id(), vlan_id);
    for (auto const& spec : kit.talker_specs()) {
        auto const* stream = kit->host().components().acmp_talker.get_stream(spec.index);
        EXPECT_NE(stream, nullptr);
        if (stream != nullptr) {
            EXPECT_EQ(stream->stream_vlan_id, vlan_id);
        }
        auto const info = kit->make_talker_srp_info(spec);
        EXPECT_EQ(info.vlan_id, vlan_id);
        EXPECT_EQ(info.priority, priority);
    }
}

/// A bridge on the wire declaring a Class A Domain, its PDUs handed to the
/// kit's MSRP handler.
void bridge_declares_domain(KitUnderTest& kit, DomainInfo const& domain)
{
    std::vector<std::vector<uint8_t>> out;
    nanoavb::MsrpHandler<> bridge{
        statusbar::srp::msrp::MsrpConfig{}, nanoavb::MsrpCallbacks{.send_packet = [&out](std::span<uint8_t const> p) {
            out.emplace_back(p.begin(), p.end());
            return true;
        }}};
    bridge.set_domain(domain);
    TimePoint now{std::chrono::seconds{1}};
    (void)bridge.declare_domain(now);
    for (int round = 0; round < 6; ++round) {
        now += std::chrono::milliseconds{120};
        bridge.tick(now);
        kit->host().components().msrp_handler.tick(now);
        for (auto const& p : out) {
            kit->host().components().msrp_handler.receive_packet(p, now);
        }
        out.clear();
    }
}

}  // namespace

TEST(avb_entity_kit_domain, acmp_and_the_talker_advertise_carry_the_configured_vlan)
{
    // VLAN 7, not the headers' default of 2: what ACMP answers (and
    // GET_STREAM_INFO shows) and what the Talker Advertise declares.
    KitUnderTest kit{make_config(7, "classic")};
    EXPECT_FALSE(kit.talker_specs().empty());
    for (auto const& spec : kit.talker_specs()) {
        auto const* stream = kit->host().components().acmp_talker.get_stream(spec.index);
        EXPECT_NE(stream, nullptr);
        if (stream != nullptr) {
            EXPECT_EQ(stream->stream_vlan_id, 7U);
        }
        auto const info = kit->make_talker_srp_info(spec);
        EXPECT_EQ(info.vlan_id, 7U);
        EXPECT_EQ(info.priority, 3U);
    }
    EXPECT_EQ(kit->talker().vlan_id(), 7U);
}

TEST(avb_entity_kit_domain, an_adopted_domain_moves_acmp_and_the_talker_advertise_with_the_frames)
{
    // Milan v1.3 4.2.7.2.1: the bridge's Class A Domain (priority 5, VLAN 5)
    // replaces ours. The frames moved already; ACMP's picture and the
    // Talker Advertise must say the same, or a listener reserves on VLAN 7
    // priority 3 for frames that arrive on VLAN 5 priority 5.
    KitUnderTest kit{make_config(7, "milan13")};
    bridge_declares_domain(kit, DomainInfo{.sr_class_id = 6, .sr_class_priority = 5, .sr_class_vid = 5});

    auto const& domain = kit->host().components().msrp_handler.domain();
    EXPECT_EQ(domain.sr_class_vid, 5U);
    EXPECT_EQ(domain.sr_class_priority, 5U);
    EXPECT_EQ(kit->talker().vlan_id(), 5U);
    EXPECT_EQ(kit->talker().stream_pcp(), 5U);
    for (auto const& spec : kit.talker_specs()) {
        auto const* stream = kit->host().components().acmp_talker.get_stream(spec.index);
        EXPECT_NE(stream, nullptr);
        if (stream != nullptr) {
            EXPECT_EQ(stream->stream_vlan_id, 5U);
        }
        auto const info = kit->make_talker_srp_info(spec);
        EXPECT_EQ(info.vlan_id, 5U);
        EXPECT_EQ(info.priority, 5U);
    }
}

TEST(avb_entity_kit_domain, a_started_kit_follows_the_domain_it_adopts)
{
    // The adoption hook is installed at construction; bringing the control
    // plane up installs the senders. A Domain adopted after start must still
    // move the frames, ACMP and the Talker Advertise, or the SRP handler
    // declares VLAN 5 for frames that go out on VLAN 7.
    KitUnderTest kit{make_config(7, "milan13")};
    EXPECT_TRUE(kit.start(TimePoint{std::chrono::seconds{1}}));
    bridge_declares_domain(kit, DomainInfo{.sr_class_id = 6, .sr_class_priority = 5, .sr_class_vid = 5});
    expect_kit_on_vlan(kit, 5, 5);
    EXPECT_TRUE(kit.stop(TimePoint{std::chrono::seconds{2}}));
}

TEST(avb_entity_kit_domain, after_a_restart_mvrp_registers_the_vlan_the_domain_declares)
{
    // stop() withdraws the VLAN and the Domain; start() registers the VLAN
    // again. It must be the one the Domain in force names, whatever that
    // is after an adoption, or MVRP and the declared Domain disagree until
    // the next adoption.
    KitUnderTest kit{make_config(7, "milan13")};
    EXPECT_TRUE(kit.start(TimePoint{std::chrono::seconds{1}}));
    bridge_declares_domain(kit, DomainInfo{.sr_class_id = 6, .sr_class_priority = 5, .sr_class_vid = 5});
    EXPECT_TRUE(kit.stop(TimePoint{std::chrono::seconds{2}}));
    EXPECT_TRUE(kit.start(TimePoint{std::chrono::seconds{3}}));

    auto const& domain = kit->host().components().msrp_handler.domain();
    auto const& mvrp = kit->host().components().mvrp_handler;
    EXPECT_EQ(kit->talker().vlan_id(), domain.sr_class_vid);
    for (uint16_t const vlan_id : {uint16_t{5}, uint16_t{7}}) {
        auto const want = vlan_id == domain.sr_class_vid ? nanoavb::VlanState::Pending : nanoavb::VlanState::Unregistered;
        EXPECT_EQ(mvrp.get_vlan_state(vlan_id), want);
    }
    EXPECT_TRUE(kit.stop(TimePoint{std::chrono::seconds{4}}));
}

TEST(avb_entity_kit_domain, a_classic_kit_keeps_its_configured_domain)
{
    KitUnderTest kit{make_config(7, "classic")};
    bridge_declares_domain(kit, DomainInfo{.sr_class_id = 6, .sr_class_priority = 5, .sr_class_vid = 5});
    EXPECT_EQ(kit->talker().vlan_id(), 7U);
    for (auto const& spec : kit.talker_specs()) {
        auto const info = kit->make_talker_srp_info(spec);
        EXPECT_EQ(info.vlan_id, 7U);
        EXPECT_EQ(info.priority, 3U);
    }
}

TEST(avb_entity_kit_tspec, a_milan_kit_declares_table_4_4_frame_sizes_and_a_classic_kit_its_headroom)
{
    // Milan v1.3 Table 4.4 sizes the Talker Advertise's TSpec exactly; the
    // classic profile keeps one sample of headroom per channel.
    KitUnderTest milan{make_config(2, "milan13")};
    KitUnderTest classic{make_config(2, "classic")};
    EXPECT_FALSE(milan.talker_specs().empty());
    for (auto const& spec : milan.talker_specs()) {
        auto const info = milan->make_talker_srp_info(spec);
        EXPECT_EQ(info.max_frame_size, milan_srp_max_frame_size(spec.format));
        EXPECT_EQ(info.max_interval_frames, 1U);
    }
    for (auto const& spec : classic.talker_specs()) {
        auto const info = classic->make_talker_srp_info(spec);
        EXPECT_EQ(info.max_frame_size, srp_max_frame_size(spec.format));
        if (spec.format.kind != StreamKind::crf) {  // CRF is exact under both
            EXPECT_NE(info.max_frame_size, milan_srp_max_frame_size(spec.format));
        }
    }
}

TEST_MAIN(statusbar_avb_entity, avb_entity_kit_test)
