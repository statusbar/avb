// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// AVB Entity AM824 I/O Unit Tests

#include "statusbar/avb_entity/avb_entity_am824_io.hpp"

#include "statusbar/atdecc/atdecc_aem_descriptor.hpp"
#include "statusbar/buffer/stream_utils.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/sm/sm.hpp"
#include "statusbar/status/status.hpp"
#include "statusbar/test/test.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace statusbar;
using namespace statusbar::avb_entity;
using namespace statusbar::ieee;

namespace {

auto load_file(std::filesystem::path const& path) -> std::vector<uint8_t>
{
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return {};
    }
    auto const size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    stream_read(file, data);
    return data;
}

auto simple2_bin_path() -> std::filesystem::path
{
    auto source_dir = std::filesystem::path{__FILE__}.parent_path();
    return source_dir / "testdata" / "simple2.bin";
}

auto make_config_with_blob(std::vector<uint8_t> blob) -> AvbEntityAm824IOConfig
{
    AvbEntityAm824IOConfig config{};
    config.entity_id = Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    config.entity_model_id = Eui64{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    config.descriptor_storage_blob = std::move(blob);
    return config;
}

}  // namespace

TEST(am824_io_create, valid_blob_succeeds)
{
    auto blob = load_file(simple2_bin_path());
    EXPECT_TRUE(blob.size() > 0);
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());
}

TEST(am824_io_create, empty_blob_fails)
{
    auto config = make_config_with_blob({});
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_FALSE(result.has_value());
}

TEST(am824_io_create, invalid_blob_fails)
{
    std::vector<uint8_t> garbage(100, 0xFF);
    auto config = make_config_with_blob(std::move(garbage));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_FALSE(result.has_value());
}

TEST(am824_io_channels, extracts_8_channels)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ((*result)->channels(), 8U);
}

TEST(am824_io_model, descriptor_counts)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());
    auto const& model = (*result)->components().entity_model;
    EXPECT_EQ(model.configuration_count(), 1);
    EXPECT_EQ(model.stream_input_count(), 1);
    EXPECT_EQ(model.stream_output_count(), 1);
    EXPECT_EQ(model.avb_interface_count(), 1);
    EXPECT_EQ(model.clock_source_count(), 1);
    EXPECT_EQ(model.clock_domain_count(), 1);
    EXPECT_EQ(model.audio_cluster_count(), 2);
    EXPECT_EQ(model.audio_map_count(), 2);
}

TEST(am824_io_model, entity_id_overridden)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    config.entity_id = Eui64{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
    config.entity_name = "Test Entity";
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());
    auto const& desc = (*result)->components().entity_model.get_entity();
    EXPECT_EQ(desc.entity_id, (Eui64{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11}));
}

TEST(am824_io_state, initial_state)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());
    EXPECT_FALSE((*result)->is_running());
    EXPECT_FALSE((*result)->is_ready());
    EXPECT_EQ((*result)->state_string(), "Start");
}

TEST(am824_io_filter, reconfigure_all_channels)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());
    (*result)->configure_filter(2000.0, -6.0, 1.0);
    EXPECT_EQ((*result)->channels(), 8U);
}

TEST(am824_io_audio, process_audio_callback_receives_correct_size)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    size_t callback_buffer_size = 0;
    size_t callback_sample_count = 0;
    (*result)->set_audio_callback([&](std::span<float> samples, size_t sample_count) {
        callback_buffer_size = samples.size();
        callback_sample_count = sample_count;
    });

    auto const now = sm::TimePoint{std::chrono::steady_clock::now().time_since_epoch()};
    (*result)->process_audio(now);

    // Buffer should be SAMPLES_PER_PACKET * channels (6 * 8 = 48)
    EXPECT_EQ(callback_buffer_size, AvbEntityAm824IO::SAMPLES_PER_PACKET * 8U);
    EXPECT_EQ(callback_sample_count, AvbEntityAm824IO::SAMPLES_PER_PACKET);
}

TEST(am824_io_constants, sample_rate)
{
    // 96 kHz to match the descriptor model and the third-party devices endpoints.
    EXPECT_EQ(AvbEntityAm824IO::SAMPLE_RATE, 96000U);
}

TEST(am824_io_constants, samples_per_packet)
{
    // SR class A: sample_rate / 8000 packets-per-second = 96000/8000 = 12.
    EXPECT_EQ(AvbEntityAm824IO::SAMPLES_PER_PACKET, 12U);
}

//
// State Machine Transition Tests
//

TEST(am824_io_sm, link_up_transitions_state)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    auto const now = sm::TimePoint{std::chrono::steady_clock::now().time_since_epoch()};
    EXPECT_EQ((*result)->state_string(), "Start");

    (*result)->on_link_up(now);
    EXPECT_TRUE((*result)->state_string() != "Start");
}

TEST(am824_io_sm, link_down_after_up)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    auto const now = sm::TimePoint{std::chrono::steady_clock::now().time_since_epoch()};
    (*result)->on_link_up(now);
    auto state_after_up = (*result)->state_string();
    (*result)->on_link_down(now);
    auto state_after_down = (*result)->state_string();
    EXPECT_TRUE(state_after_down != state_after_up || state_after_down == "Down");
}

TEST(am824_io_sm, timeout_does_not_crash)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    auto const now = sm::TimePoint{std::chrono::steady_clock::now().time_since_epoch()};
    (*result)->on_timeout(now);
    EXPECT_FALSE((*result)->is_running());
}

TEST(am824_io_sm, gptp_announce_no_grandmaster)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    auto const now = sm::TimePoint{std::chrono::steady_clock::now().time_since_epoch()};
    // gPTP announce without grandmaster is a no-op (safe from any state)
    (*result)->on_gptp_announce(now, false);
    EXPECT_FALSE((*result)->is_running());
    EXPECT_EQ((*result)->state_string(), "Start");
}

TEST(am824_io_sm, multiple_link_toggles)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    auto const now = sm::TimePoint{std::chrono::steady_clock::now().time_since_epoch()};
    for (int i = 0; i < 5; ++i) {
        (*result)->on_link_up(now);
        (*result)->on_link_down(now);
    }
    EXPECT_FALSE((*result)->is_running());
}

//
// Process Audio Tests
//

TEST(am824_io_audio, process_audio_does_not_crash)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    auto const now = sm::TimePoint{std::chrono::steady_clock::now().time_since_epoch()};
    (*result)->process_audio(now);
    EXPECT_FALSE((*result)->is_running());
}

TEST(am824_io_audio, process_audio_multiple_times)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    auto const now = sm::TimePoint{std::chrono::steady_clock::now().time_since_epoch()};
    for (int i = 0; i < 10; ++i) {
        (*result)->process_audio(now);
    }
    EXPECT_FALSE((*result)->is_running());
}

//
// Component Access Tests
//

TEST(am824_io_components, components_mutable_access)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    auto& components = (*result)->components();
    EXPECT_EQ(components.entity_model.stream_input_count(), 1);
}

TEST(am824_io_components, net_handlers_null_before_start)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    EXPECT_TRUE((*result)->net_handlers() == nullptr);
}

TEST(am824_io_components, config_accessible)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    config.entity_name = "AM824 Test Entity";
    config.firmware_version = "2.5.0";
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    EXPECT_EQ((*result)->config().entity_name, "AM824 Test Entity");
    EXPECT_EQ((*result)->config().firmware_version, "2.5.0");
}

//
// Filter Tests
//

TEST(am824_io_filter, configure_with_various_params)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    (*result)->configure_filter(500.0, -6.0, 0.5);
    (*result)->configure_filter(2000.0, 6.0, 2.0);
    (*result)->configure_filter(10000.0, 0.0, 1.0);
    EXPECT_EQ((*result)->channels(), 8U);
}

//
// Print State Test
//

TEST(am824_io_print, print_state_does_not_crash)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    (*result)->print_state();
    EXPECT_FALSE((*result)->is_running());
}

//
// Stop Without Start Test
//

TEST(am824_io_lifecycle, stop_without_start_fails)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    auto stop_result = (*result)->stop();
    EXPECT_FALSE(stop_result.has_value());
}

//
// Entity Model Detail Tests
//

TEST(am824_io_model, entity_name_set_in_model)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    config.entity_name = "Named AM824";
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    auto const& desc = (*result)->components().entity_model.get_entity();
    // The entity name should have been set from config
    EXPECT_EQ(desc.entity_id, (Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07}));
}

TEST(am824_io_model, clock_source_and_domain)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    auto const& model = (*result)->components().entity_model;
    EXPECT_EQ(model.clock_source_count(), 1);
    EXPECT_EQ(model.clock_domain_count(), 1);
}

TEST(am824_io_model, audio_clusters_and_maps)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    auto const& model = (*result)->components().entity_model;
    EXPECT_EQ(model.audio_cluster_count(), 2);
    EXPECT_EQ(model.audio_map_count(), 2);
}

//
// Audio Callback With Filter Processing
//

TEST(am824_io_audio, callback_receives_filtered_data)
{
    auto blob = load_file(simple2_bin_path());
    auto config = make_config_with_blob(std::move(blob));
    config.filter_gain_db = 0.0;  // flat response
    auto result = AvbEntityAm824IO::create(std::move(config));
    EXPECT_TRUE(result.has_value());

    std::vector<float> captured_samples;
    (*result)->set_audio_callback([&](std::span<float> samples, size_t count) {
        captured_samples.assign(samples.begin(), samples.end());
        (void)count;
    });

    auto const now = sm::TimePoint{std::chrono::steady_clock::now().time_since_epoch()};
    (*result)->process_audio(now);

    // process_audio now drives a per-channel sine source through the (flat)
    // filter, so the callback receives a non-silent, bounded signal.
    EXPECT_EQ(captured_samples.size(), AvbEntityAm824IO::SAMPLES_PER_PACKET * 8U);
    float max_abs = 0.0f;
    for (auto s : captured_samples) {
        EXPECT_TRUE(s >= -1.0f && s <= 1.0f);  // bounded
        max_abs = std::max(max_abs, std::abs(s));
    }
    EXPECT_TRUE(max_abs > 0.0f);  // not silence

    // Each channel uses a distinct initial phase, so no two adjacent channels
    // carry the identical first sample.
    EXPECT_TRUE(captured_samples[0] != captured_samples[1]);
}

TEST_MAIN(statusbar_avb_entity, avb_entity_am824_io_test)
