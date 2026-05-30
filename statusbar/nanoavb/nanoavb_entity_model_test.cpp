// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/atdecc/atdecc.hpp"
#include "statusbar/nanoavb/nanoavb.hpp"
#include "statusbar/test/test.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <system_error>

using namespace statusbar::nanoavb;
using namespace statusbar::atdecc::aem;

//
// EntityModel Creation Tests
//
TEST(nanoavb_entity_model_create, default_construction)
{
    EntityModel model;
    EXPECT_EQ(model.configuration_count(), 0);
    EXPECT_EQ(model.audio_unit_count(), 0);
    EXPECT_EQ(model.stream_input_count(), 0);
    EXPECT_EQ(model.stream_output_count(), 0);
}

TEST(nanoavb_entity_model_create, with_config)
{
    EntityModelConfig config;
    config.max_configurations = 2;
    config.max_stream_inputs = 4;

    EntityModel model{config};
    EXPECT_EQ(model.config().max_configurations, 2);
    EXPECT_EQ(model.config().max_stream_inputs, 4);
}

//
// Entity Descriptor Tests
//
TEST(nanoavb_entity_model_entity, set_and_get)
{
    EntityModel model;

    DescriptorEntity entity{};
    entity.entity_name = AtdeccString{"Test Entity"};
    entity.talker_stream_sources = 2;
    entity.listener_stream_sinks = 2;

    model.set_entity(entity);

    auto const& stored = model.get_entity();
    EXPECT_EQ(stored.entity_name.as_string_view(), "Test Entity");
    EXPECT_EQ(stored.talker_stream_sources.get(), 2);
}

TEST(nanoavb_entity_model_entity, get_mut)
{
    EntityModel model;

    auto& entity = model.get_entity_mut();
    entity.entity_name = AtdeccString{"Modified Entity"};

    EXPECT_EQ(model.get_entity().entity_name.as_string_view(), "Modified Entity");
}

//
// Configuration Descriptor Tests
//
TEST(nanoavb_entity_model_descriptors, add_configuration)
{
    EntityModel model;

    DescriptorConfiguration config{};
    config.object_name = AtdeccString{"Default Config"};

    auto result = model.add_configuration(config);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
    EXPECT_EQ(model.configuration_count(), 1);

    auto get_result = model.get_configuration(0);
    EXPECT_TRUE(get_result.has_value());
    EXPECT_EQ((*get_result)->object_name.as_string_view(), "Default Config");
    EXPECT_EQ((*get_result)->descriptor_index.get(), 0);
}

TEST(nanoavb_entity_model_descriptors, configuration_storage_full)
{
    EntityModelConfig cfg;
    cfg.max_configurations = 1;
    EntityModel model{cfg};

    DescriptorConfiguration config{};
    auto result1 = model.add_configuration(config);
    EXPECT_TRUE(result1.has_value());

    auto result2 = model.add_configuration(config);
    EXPECT_FALSE(result2.has_value());
    EXPECT_TRUE(result2.error() == make_error_code(NanoAvbError::DescriptorStorageFull));
}

TEST(nanoavb_entity_model_descriptors, configuration_index_out_of_range)
{
    EntityModel model;

    auto result = model.get_configuration(99);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error() == make_error_code(NanoAvbError::InvalidDescriptorIndex));
}

//
// Stream Descriptor Tests
//
TEST(nanoavb_entity_model_streams, add_stream_input)
{
    EntityModel model;

    DescriptorStream stream{};
    stream.object_name = AtdeccString{"Audio Input 1"};

    auto result = model.add_stream_input(stream);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);

    auto get_result = model.get_stream_input(0);
    EXPECT_TRUE(get_result.has_value());
    EXPECT_EQ((*get_result)->object_name.as_string_view(), "Audio Input 1");
    EXPECT_EQ((*get_result)->descriptor_type.get(), DESCRIPTOR_STREAM_INPUT);
}

TEST(nanoavb_entity_model_streams, add_stream_output)
{
    EntityModel model;

    DescriptorStream stream{};
    stream.object_name = AtdeccString{"Audio Output 1"};

    auto result = model.add_stream_output(stream);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);

    auto get_result = model.get_stream_output(0);
    EXPECT_TRUE(get_result.has_value());
    EXPECT_EQ((*get_result)->descriptor_type.get(), DESCRIPTOR_STREAM_OUTPUT);
}

TEST(nanoavb_entity_model_streams, multiple_streams)
{
    EntityModel model;

    for (int i = 0; i < 4; ++i) {
        DescriptorStream stream{};
        auto result = model.add_stream_input(stream);
        EXPECT_TRUE(result.has_value());
        EXPECT_EQ(*result, static_cast<uint16_t>(i));
    }

    EXPECT_EQ(model.stream_input_count(), 4);
}

//
// Jack Descriptor Tests
//
TEST(nanoavb_entity_model_jacks, add_jack_input)
{
    EntityModel model;

    DescriptorJack jack{};
    jack.object_name = AtdeccString{"Analog In 1"};
    jack.jack_type = JACK_TYPE_BALANCED_ANALOG;

    auto result = model.add_jack_input(jack);
    EXPECT_TRUE(result.has_value());

    auto get_result = model.get_jack_input(0);
    EXPECT_TRUE(get_result.has_value());
    EXPECT_EQ((*get_result)->descriptor_type.get(), DESCRIPTOR_JACK_INPUT);
    EXPECT_EQ((*get_result)->jack_type.get(), JACK_TYPE_BALANCED_ANALOG);
}

TEST(nanoavb_entity_model_jacks, add_jack_output)
{
    EntityModel model;

    DescriptorJack jack{};
    jack.object_name = AtdeccString{"Analog Out 1"};

    auto result = model.add_jack_output(jack);
    EXPECT_TRUE(result.has_value());

    auto get_result = model.get_jack_output(0);
    EXPECT_TRUE(get_result.has_value());
    EXPECT_EQ((*get_result)->descriptor_type.get(), DESCRIPTOR_JACK_OUTPUT);
}

//
// Raw Descriptor Access Tests
//
TEST(nanoavb_entity_model_raw, get_entity_raw)
{
    EntityModel model;
    model.get_entity_mut().entity_name = AtdeccString{"Test"};

    auto result = model.get_descriptor_raw(DESCRIPTOR_ENTITY, 0);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), sizeof(DescriptorEntity));
}

TEST(nanoavb_entity_model_raw, get_configuration_raw)
{
    EntityModel model;

    DescriptorConfiguration config{};
    (void)model.add_configuration(config);

    // DescriptorConfiguration now carries an inline descriptor_counts
    // trailer, so sizeof() is much larger than the on-wire size.
    // get_descriptor_raw returns wire_span() = LENGTH + count*4; with
    // descriptor_counts_count = 0 that collapses to LENGTH = 74.
    auto result = model.get_descriptor_raw(DESCRIPTOR_CONFIGURATION, 0);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), DescriptorConfiguration::LENGTH);
}

TEST(nanoavb_entity_model_raw, invalid_descriptor_type)
{
    EntityModel model;

    auto result = model.get_descriptor_raw(0xFFFF, 0);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error() == make_error_code(NanoAvbError::InvalidDescriptorType));
}

TEST(nanoavb_entity_model_raw, invalid_descriptor_index)
{
    EntityModel model;

    auto result = model.get_descriptor_raw(DESCRIPTOR_CONFIGURATION, 99);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error() == make_error_code(NanoAvbError::InvalidDescriptorIndex));
}

//
// Audio Unit Descriptor Tests
//
TEST(nanoavb_entity_model_audio_unit, add_and_get)
{
    EntityModel model;

    DescriptorAudioUnit au{};
    au.object_name = AtdeccString{"Audio Unit 0"};

    auto result = model.add_audio_unit(au);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
    EXPECT_EQ(model.audio_unit_count(), 1);

    auto get_result = model.get_audio_unit(0);
    EXPECT_TRUE(get_result.has_value());
    EXPECT_EQ((*get_result)->object_name.as_string_view(), "Audio Unit 0");
}

TEST(nanoavb_entity_model_audio_unit, index_out_of_range)
{
    EntityModel model;
    auto result = model.get_audio_unit(99);
    EXPECT_FALSE(result.has_value());
}

//
// AVB Interface Descriptor Tests
//
TEST(nanoavb_entity_model_avb_iface, add_and_get)
{
    EntityModel model;

    DescriptorAvbInterface iface{};
    iface.object_name = AtdeccString{"AVB Interface 0"};

    auto result = model.add_avb_interface(iface);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
    EXPECT_EQ(model.avb_interface_count(), 1);

    auto get_result = model.get_avb_interface(0);
    EXPECT_TRUE(get_result.has_value());
    EXPECT_EQ((*get_result)->object_name.as_string_view(), "AVB Interface 0");
}

//
// Clock Source Descriptor Tests
//
TEST(nanoavb_entity_model_clock_src, add_and_get)
{
    EntityModel model;

    DescriptorClockSource cs{};
    cs.object_name = AtdeccString{"Internal Clock"};

    auto result = model.add_clock_source(cs);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
    EXPECT_EQ(model.clock_source_count(), 1);

    auto get_result = model.get_clock_source(0);
    EXPECT_TRUE(get_result.has_value());
    EXPECT_EQ((*get_result)->object_name.as_string_view(), "Internal Clock");
}

//
// Clock Domain Descriptor Tests
//
TEST(nanoavb_entity_model_clock_dom, add_and_get)
{
    EntityModel model;

    DescriptorClockDomain cd{};
    cd.object_name = AtdeccString{"Clock Domain 0"};

    auto result = model.add_clock_domain(cd);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
    EXPECT_EQ(model.clock_domain_count(), 1);

    auto get_result = model.get_clock_domain(0);
    EXPECT_TRUE(get_result.has_value());
    EXPECT_EQ((*get_result)->object_name.as_string_view(), "Clock Domain 0");
}

//
// Locale Descriptor Tests
//
TEST(nanoavb_entity_model_locale, add_and_get)
{
    EntityModel model;

    DescriptorLocale loc{};
    loc.locale_identifier = AtdeccString{"en-US"};

    auto result = model.add_locale(loc);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
    EXPECT_EQ(model.locale_count(), 1);

    auto get_result = model.get_locale(0);
    EXPECT_TRUE(get_result.has_value());
}

//
// Strings Descriptor Tests
//
TEST(nanoavb_entity_model_strings, add_and_get)
{
    EntityModel model;

    DescriptorStrings str{};

    auto result = model.add_strings(str);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
    EXPECT_EQ(model.strings_count(), 1);

    auto get_result = model.get_strings(0);
    EXPECT_TRUE(get_result.has_value());
}

//
// Stream Port Descriptor Tests
//
TEST(nanoavb_entity_model_stream_port, add_input_and_output)
{
    EntityModel model;

    DescriptorStreamPort port_in{};
    auto result_in = model.add_stream_port_input(port_in);
    EXPECT_TRUE(result_in.has_value());
    EXPECT_EQ(*result_in, 0);
    EXPECT_EQ(model.stream_port_input_count(), 1);

    DescriptorStreamPort port_out{};
    auto result_out = model.add_stream_port_output(port_out);
    EXPECT_TRUE(result_out.has_value());
    EXPECT_EQ(*result_out, 0);
    EXPECT_EQ(model.stream_port_output_count(), 1);

    auto get_in = model.get_stream_port_input(0);
    EXPECT_TRUE(get_in.has_value());
    auto get_out = model.get_stream_port_output(0);
    EXPECT_TRUE(get_out.has_value());
}

//
// Audio Cluster Descriptor Tests
//
TEST(nanoavb_entity_model_audio_cluster, add_and_get)
{
    EntityModel model;

    DescriptorAudioCluster cluster{};
    cluster.object_name = AtdeccString{"Cluster 0"};

    auto result = model.add_audio_cluster(cluster);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
    EXPECT_EQ(model.audio_cluster_count(), 1);

    auto get_result = model.get_audio_cluster(0);
    EXPECT_TRUE(get_result.has_value());
    EXPECT_EQ((*get_result)->object_name.as_string_view(), "Cluster 0");
}

//
// Audio Map Descriptor Tests
//
TEST(nanoavb_entity_model_audio_map, add_and_get)
{
    EntityModel model;

    DescriptorAudioMap amap{};

    auto result = model.add_audio_map(amap);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
    EXPECT_EQ(model.audio_map_count(), 1);

    auto get_result = model.get_audio_map(0);
    EXPECT_TRUE(get_result.has_value());
}

//
// Control Descriptor Tests
//
TEST(nanoavb_entity_model_control, add_and_get)
{
    EntityModel model;

    DescriptorControl ctrl{};
    ctrl.object_name = AtdeccString{"Volume"};

    auto result = model.add_control(ctrl);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
    EXPECT_EQ(model.control_count(), 1);

    auto get_result = model.get_control(0);
    EXPECT_TRUE(get_result.has_value());
    EXPECT_EQ((*get_result)->object_name.as_string_view(), "Volume");
}

//
// Builder Tests
//
TEST(nanoavb_entity_model_builder, fluent_construction)
{
    DescriptorEntity entity{};
    entity.entity_name = AtdeccString{"Built Entity"};
    entity.talker_stream_sources = 2;
    entity.listener_stream_sinks = 2;

    DescriptorConfiguration config{};
    config.object_name = AtdeccString{"Config 0"};

    DescriptorStream stream_in{};
    stream_in.object_name = AtdeccString{"Input 0"};

    DescriptorStream stream_out{};
    stream_out.object_name = AtdeccString{"Output 0"};

    auto model =
        EntityModelBuilder{}.entity(entity).configuration(config).stream_input(stream_in).stream_output(stream_out).build();

    EXPECT_EQ(model.get_entity().entity_name.as_string_view(), "Built Entity");
    EXPECT_EQ(model.configuration_count(), 1);
    EXPECT_EQ(model.stream_input_count(), 1);
    EXPECT_EQ(model.stream_output_count(), 1);
}

TEST(nanoavb_entity_model_builder, with_custom_config)
{
    EntityModelConfig cfg;
    cfg.max_stream_inputs = 16;
    cfg.max_stream_outputs = 16;

    auto model = EntityModelBuilder{cfg}.build();

    EXPECT_EQ(model.config().max_stream_inputs, 16);
    EXPECT_EQ(model.config().max_stream_outputs, 16);
}

TEST(nanoavb_entity_model_builder, all_descriptor_types)
{
    DescriptorEntity entity{};
    entity.entity_name = AtdeccString{"Full Entity"};
    entity.talker_stream_sources = 1;
    entity.listener_stream_sinks = 1;

    DescriptorConfiguration config{};
    DescriptorAudioUnit au{};
    DescriptorStream stream_in{};
    DescriptorStream stream_out{};
    DescriptorJack jack_in{};
    DescriptorJack jack_out{};
    DescriptorAvbInterface avb_iface{};
    DescriptorClockSource clock_src{};
    DescriptorClockDomain clock_dom{};
    DescriptorLocale loc{};
    DescriptorStrings str{};
    DescriptorStreamPort port_in{};
    DescriptorStreamPort port_out{};
    DescriptorAudioCluster cluster{};
    DescriptorAudioMap amap{};
    DescriptorControl ctrl{};

    auto model = EntityModelBuilder{}
                     .entity(entity)
                     .configuration(config)
                     .audio_unit(au)
                     .stream_input(stream_in)
                     .stream_output(stream_out)
                     .jack_input(jack_in)
                     .jack_output(jack_out)
                     .avb_interface(avb_iface)
                     .clock_source(clock_src)
                     .clock_domain(clock_dom)
                     .locale(loc)
                     .strings(str)
                     .stream_port_input(port_in)
                     .stream_port_output(port_out)
                     .audio_cluster(cluster)
                     .audio_map(amap)
                     .control(ctrl)
                     .build();

    EXPECT_EQ(model.get_entity().entity_name.as_string_view(), "Full Entity");
    EXPECT_EQ(model.configuration_count(), 1);
    EXPECT_EQ(model.audio_unit_count(), 1);
    EXPECT_EQ(model.stream_input_count(), 1);
    EXPECT_EQ(model.stream_output_count(), 1);
    EXPECT_EQ(model.jack_input_count(), 1);
    EXPECT_EQ(model.jack_output_count(), 1);
    EXPECT_EQ(model.avb_interface_count(), 1);
    EXPECT_EQ(model.clock_source_count(), 1);
    EXPECT_EQ(model.clock_domain_count(), 1);
    EXPECT_EQ(model.locale_count(), 1);
    EXPECT_EQ(model.strings_count(), 1);
    EXPECT_EQ(model.stream_port_input_count(), 1);
    EXPECT_EQ(model.stream_port_output_count(), 1);
    EXPECT_EQ(model.audio_cluster_count(), 1);
    EXPECT_EQ(model.audio_map_count(), 1);
    EXPECT_EQ(model.control_count(), 1);
}

//
// Test Runner
//
int statusbar_nanoavb_nanoavb_entity_model_test(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    int result = 0;
    result |= ::statusbar::test::TestRegister::run_section("nanoavb_entity_model_test");
    return result;
}