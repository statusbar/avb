// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/atdecc/atdecc.hpp"
#include "statusbar/buffer/buffer.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/nanoavb/nanoavb.hpp"
#include "statusbar/test/test.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <vector>

using namespace statusbar;
using namespace statusbar::nanoavb;
using namespace statusbar::atdecc;
using namespace statusbar::atdecc::aem;
using statusbar::ieee::Eui48;
using statusbar::ieee::Eui64;

//
// Helper Functions
//
static EntityModel create_test_model()
{
    DescriptorEntity entity{};
    entity.entity_name = AtdeccString{"Test Entity"};
    entity.configurations_count = 1;

    DescriptorConfiguration config{};
    config.object_name = AtdeccString{"Default"};

    DescriptorStream stream_in{};
    stream_in.object_name = AtdeccString{"Audio In"};

    return EntityModelBuilder{}.entity(entity).configuration(config).stream_input(stream_in).build();
}

static AemDu create_aem_header(uint16_t command_code)
{
    AemDu header{};
    header.init_command(command_code, 12);  // 12 bytes for controller entity id + sequence id + command type
    return header;
}

//
// Test helper: wrap AemCommandHandler::handle_command with a locally-owned
// output buffer and expose an object shape matching the old AemCommandResult,
// so existing test assertions (result.status, result.response_data()) keep
// working after the output-buffer API refactor.
//
struct TestCommandResult
{
    uint8_t status{AEM_STATUS_SUCCESS};
    std::vector<uint8_t> bytes;

    [[nodiscard]] auto response_data() const noexcept -> std::span<uint8_t const> { return bytes; }
};

static auto make_test_result(AemCommandHandler& h, AemDu const& header, std::span<uint8_t const> command_data) -> TestCommandResult
{
    std::array<uint8_t, MAX_AEM_RESPONSE_SIZE> buf{};
    auto const response = h.handle_command(header, command_data, buf);
    return {.status = response.status, .bytes = std::vector<uint8_t>(buf.data(), buf.data() + response.size)};
}

//
// Entity Available Tests
//
TEST(nanoavb_entity_aem, entity_available)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_ENTITY_AVAILABLE);
    auto result = make_test_result(handler, header, {});

    EXPECT_EQ(result.status, AEM_STATUS_SUCCESS);
    EXPECT_TRUE(result.response_data().empty());
}

//
// Read Descriptor Tests
//
TEST(nanoavb_entity_commands, read_entity_descriptor)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_READ_DESCRIPTOR);

    // Build READ_DESCRIPTOR command data
    std::array<uint8_t, 8> command_data = {
        0x00,
        0x00,  // Configuration index
        0x00,
        0x00,  // Reserved
        0x00,
        0x00,  // Descriptor type = ENTITY
        0x00,
        0x00  // Descriptor index = 0
    };

    auto result = make_test_result(handler, header, command_data);

    EXPECT_EQ(result.status, AEM_STATUS_SUCCESS);
    // Response should be 8 byte header + entity descriptor size
    EXPECT_EQ(result.response_data().size(), 8 + sizeof(DescriptorEntity));
}

TEST(nanoavb_entity_commands, read_configuration_descriptor)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_READ_DESCRIPTOR);

    // Build READ_DESCRIPTOR command data for CONFIGURATION
    std::array<uint8_t, 8> command_data = {
        0x00,
        0x00,  // Configuration index
        0x00,
        0x00,  // Reserved
        0x00,
        0x01,  // Descriptor type = CONFIGURATION
        0x00,
        0x00  // Descriptor index = 0
    };

    auto result = make_test_result(handler, header, command_data);

    EXPECT_EQ(result.status, AEM_STATUS_SUCCESS);
    // DescriptorConfiguration now has an inline descriptor_counts trailer,
    // so sizeof() is larger than the wire size. The response body is the
    // 8-byte READ_DESCRIPTOR header + wire_size() = LENGTH for an empty
    // config (descriptor_counts_count == 0).
    EXPECT_EQ(result.response_data().size(), 8 + DescriptorConfiguration::LENGTH);
}

TEST(nanoavb_entity_commands, read_stream_input_descriptor)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_READ_DESCRIPTOR);

    // Build READ_DESCRIPTOR command data for STREAM_INPUT
    std::array<uint8_t, 8> command_data = {
        0x00,
        0x00,  // Configuration index
        0x00,
        0x00,  // Reserved
        0x00,
        0x05,  // Descriptor type = STREAM_INPUT
        0x00,
        0x00  // Descriptor index = 0
    };

    auto result = make_test_result(handler, header, command_data);

    EXPECT_EQ(result.status, AEM_STATUS_SUCCESS);
    // DescriptorStream now carries an inline stream_formats trailer,
    // so sizeof() is larger than the on-wire size. For a freshly
    // constructed stream with number_of_formats = 0, wire_size() equals
    // LENGTH (138 bytes), so the full response is 8 (READ_DESCRIPTOR
    // header) + 138.
    EXPECT_EQ(result.response_data().size(), 8 + DescriptorStream::LENGTH);
}

TEST(nanoavb_entity_commands, read_invalid_descriptor)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_READ_DESCRIPTOR);

    // Request a descriptor that doesn't exist
    std::array<uint8_t, 8> command_data = {
        0x00,
        0x00,  // Configuration index
        0x00,
        0x00,  // Reserved
        0x00,
        0x05,  // Descriptor type = STREAM_INPUT
        0x00,
        0x63  // Descriptor index = 99 (invalid)
    };

    auto result = make_test_result(handler, header, command_data);

    EXPECT_EQ(result.status, AEM_STATUS_NO_SUCH_DESCRIPTOR);
}

TEST(nanoavb_entity_commands, read_descriptor_bad_arguments)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_READ_DESCRIPTOR);

    // Too short command data
    std::array<uint8_t, 4> command_data = {0x00, 0x00, 0x00, 0x00};

    auto result = make_test_result(handler, header, command_data);

    EXPECT_EQ(result.status, AEM_STATUS_BAD_ARGUMENTS);
}

//
// Acquire Entity Tests
//
TEST(nanoavb_entity_acquire, acquire_success)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    EXPECT_FALSE(handler.is_acquired());

    auto header = create_aem_header(AEM_COMMAND_ACQUIRE_ENTITY);
    header.controller_entity_id = statusbar::ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

    // Build ACQUIRE_ENTITY command data
    std::array<uint8_t, 16> command_data = {
        0x00,
        0x00,
        0x00,
        0x00,  // Flags
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,  // Owner ID
        0x00,
        0x00,  // Descriptor type = ENTITY
        0x00,
        0x00  // Descriptor index = 0
    };

    auto result = make_test_result(handler, header, command_data);

    EXPECT_EQ(result.status, AEM_STATUS_SUCCESS);
    EXPECT_TRUE(handler.is_acquired());
    EXPECT_EQ(handler.acquiring_controller(), header.controller_entity_id);
}

TEST(nanoavb_entity_acquire, acquire_already_acquired)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header1 = create_aem_header(AEM_COMMAND_ACQUIRE_ENTITY);
    header1.controller_entity_id = statusbar::ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

    auto header2 = create_aem_header(AEM_COMMAND_ACQUIRE_ENTITY);
    header2.controller_entity_id = statusbar::ieee::Eui64{0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};

    std::array<uint8_t, 16> command_data = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    // First acquire succeeds
    auto result1 = make_test_result(handler, header1, command_data);
    EXPECT_EQ(result1.status, AEM_STATUS_SUCCESS);

    // Second acquire fails (different controller)
    auto result2 = make_test_result(handler, header2, command_data);
    EXPECT_EQ(result2.status, AEM_STATUS_ENTITY_ACQUIRED);

    // Same controller acquire succeeds
    auto result3 = make_test_result(handler, header1, command_data);
    EXPECT_EQ(result3.status, AEM_STATUS_SUCCESS);
}

TEST(nanoavb_entity_acquire, release_acquisition)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_ACQUIRE_ENTITY);
    header.controller_entity_id = statusbar::ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

    // Acquire first
    std::array<uint8_t, 16> acquire_data = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    (void)make_test_result(handler, header, acquire_data);
    EXPECT_TRUE(handler.is_acquired());

    // Release
    std::array<uint8_t, 16> release_data = {
        0x80,
        0x00,
        0x00,
        0x00,  // RELEASE flag
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00};

    auto result = make_test_result(handler, header, release_data);
    EXPECT_EQ(result.status, AEM_STATUS_SUCCESS);
    EXPECT_FALSE(handler.is_acquired());
}

TEST(nanoavb_entity_acquire, controller_available_handshake_owner_alive)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto controller_a = statusbar::ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    auto controller_b = statusbar::ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x08};

    // Controller A acquires
    auto header_a = create_aem_header(AEM_COMMAND_ACQUIRE_ENTITY);
    header_a.controller_entity_id = controller_a;
    std::array<uint8_t, 16> acquire_data = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto now = sm::TimePoint{};
    handler.set_event_time(now);
    (void)make_test_result(handler, header_a, acquire_data);
    EXPECT_TRUE(handler.is_acquired());

    // Set up CONTROLLER_AVAILABLE callback
    bool controller_available_sent = false;
    Eui64 controller_available_target{};
    handler.send_controller_available = [&](Eui64 const& owner_id) {
        controller_available_sent = true;
        controller_available_target = owner_id;
        return true;
    };

    // Controller B tries to acquire
    auto header_b = create_aem_header(AEM_COMMAND_ACQUIRE_ENTITY);
    header_b.controller_entity_id = controller_b;
    handler.set_event_time(now + std::chrono::milliseconds(100));
    auto result = make_test_result(handler, header_b, acquire_data);

    // Should send CONTROLLER_AVAILABLE to A and return IN_PROGRESS
    EXPECT_TRUE(controller_available_sent);
    EXPECT_TRUE(controller_available_target == controller_a);
    EXPECT_EQ(result.status, AEM_STATUS_IN_PROGRESS);
    EXPECT_TRUE(handler.has_pending_acquire());

    // A responds (is alive) - B should be denied
    handler.controller_available_response_received();
    EXPECT_FALSE(handler.has_pending_acquire());
    EXPECT_TRUE(handler.is_acquired());
    EXPECT_TRUE(handler.acquiring_controller() == controller_a);  // A still owns
}

TEST(nanoavb_entity_acquire, controller_available_handshake_owner_timeout)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto controller_a = statusbar::ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    auto controller_b = statusbar::ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x08};

    auto header_a = create_aem_header(AEM_COMMAND_ACQUIRE_ENTITY);
    header_a.controller_entity_id = controller_a;
    std::array<uint8_t, 16> acquire_data = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto now = sm::TimePoint{};
    handler.set_event_time(now);
    (void)make_test_result(handler, header_a, acquire_data);

    handler.send_controller_available = [](Eui64 const&) { return true; };

    auto header_b = create_aem_header(AEM_COMMAND_ACQUIRE_ENTITY);
    header_b.controller_entity_id = controller_b;
    handler.set_event_time(now + std::chrono::milliseconds(100));
    (void)make_test_result(handler, header_b, acquire_data);
    EXPECT_TRUE(handler.has_pending_acquire());

    // Timeout via tick - acquisition transfers to B
    handler.tick(now + std::chrono::milliseconds(400));
    EXPECT_FALSE(handler.has_pending_acquire());
    EXPECT_TRUE(handler.is_acquired());
    EXPECT_TRUE(handler.acquiring_controller() == controller_b);
}

TEST(nanoavb_entity_acquire, persistent_skips_controller_available)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto controller_a = statusbar::ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    auto controller_b = statusbar::ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x08};

    // Controller A acquires with PERSISTENT flag
    auto header_a = create_aem_header(AEM_COMMAND_ACQUIRE_ENTITY);
    header_a.controller_entity_id = controller_a;
    std::array<uint8_t, 16> persistent_data = {
        0x00,
        0x00,
        0x00,
        0x01,  // PERSISTENT flag
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00};

    handler.set_event_time(sm::TimePoint{});
    (void)make_test_result(handler, header_a, persistent_data);

    bool controller_available_sent = false;
    handler.send_controller_available = [&](Eui64 const&) {
        controller_available_sent = true;
        return true;
    };

    // Controller B tries to acquire (non-persistent)
    auto header_b = create_aem_header(AEM_COMMAND_ACQUIRE_ENTITY);
    header_b.controller_entity_id = controller_b;
    std::array<uint8_t, 16> acquire_data = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto result = make_test_result(handler, header_b, acquire_data);

    // PERSISTENT acquisition should deny immediately without CONTROLLER_AVAILABLE
    EXPECT_FALSE(controller_available_sent);
    EXPECT_EQ(result.status, AEM_STATUS_ENTITY_ACQUIRED);
    EXPECT_FALSE(handler.has_pending_acquire());
}

//
// Unsolicited Notification Tests
//
TEST(nanoavb_entity_unsolicited, register_success)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_REGISTER_UNSOLICITED_NOTIFICATION);
    header.controller_entity_id = statusbar::ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

    auto result = make_test_result(handler, header, {});
    EXPECT_EQ(result.status, AEM_STATUS_SUCCESS);
    EXPECT_EQ(handler.unsolicited_registration_count(), 1U);
}

TEST(nanoavb_entity_unsolicited, register_idempotent)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_REGISTER_UNSOLICITED_NOTIFICATION);
    header.controller_entity_id = statusbar::ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

    (void)make_test_result(handler, header, {});
    auto result = make_test_result(handler, header, {});
    EXPECT_EQ(result.status, AEM_STATUS_SUCCESS);
    EXPECT_EQ(handler.unsolicited_registration_count(), 1U);
}

TEST(nanoavb_entity_unsolicited, deregister_success)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_REGISTER_UNSOLICITED_NOTIFICATION);
    header.controller_entity_id = statusbar::ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    (void)make_test_result(handler, header, {});
    EXPECT_EQ(handler.unsolicited_registration_count(), 1U);

    auto dereg_header = create_aem_header(AEM_COMMAND_DEREGISTER_UNSOLICITED_NOTIFICATION);
    dereg_header.controller_entity_id = header.controller_entity_id;
    auto result = make_test_result(handler, dereg_header, {});
    EXPECT_EQ(result.status, AEM_STATUS_SUCCESS);
    EXPECT_EQ(handler.unsolicited_registration_count(), 0U);
}

TEST(nanoavb_entity_unsolicited, max_registrations)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    // Fill all slots
    for (size_t i = 0; i < AemCommandHandler::MAX_UNSOLICITED_REGISTRATIONS; ++i) {
        auto header = create_aem_header(AEM_COMMAND_REGISTER_UNSOLICITED_NOTIFICATION);
        header.controller_entity_id = statusbar::ieee::Eui64{0, 0, 0, 0, 0, 0, 0, static_cast<uint8_t>(i + 1)};
        auto result = make_test_result(handler, header, {});
        EXPECT_EQ(result.status, AEM_STATUS_SUCCESS);
    }

    EXPECT_EQ(handler.unsolicited_registration_count(), AemCommandHandler::MAX_UNSOLICITED_REGISTRATIONS);

    // One more should fail
    auto header = create_aem_header(AEM_COMMAND_REGISTER_UNSOLICITED_NOTIFICATION);
    header.controller_entity_id = statusbar::ieee::Eui64{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    auto result = make_test_result(handler, header, {});
    EXPECT_EQ(result.status, AEM_STATUS_NO_RESOURCES);
}

//
// Lock Entity Tests
//
TEST(nanoavb_entity_lock, lock_success)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    EXPECT_FALSE(handler.is_locked());

    auto header = create_aem_header(AEM_COMMAND_LOCK_ENTITY);
    header.controller_entity_id = statusbar::ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

    std::array<uint8_t, 16> command_data = {
        0x00,
        0x00,
        0x00,
        0x00,  // Flags (no UNLOCK)
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,  // Locked ID
        0x00,
        0x00,  // Descriptor type = ENTITY
        0x00,
        0x00  // Descriptor index = 0
    };

    auto result = make_test_result(handler, header, command_data);

    EXPECT_EQ(result.status, AEM_STATUS_SUCCESS);
    EXPECT_TRUE(handler.is_locked());
}

TEST(nanoavb_entity_lock, unlock_success)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_LOCK_ENTITY);
    header.controller_entity_id = statusbar::ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

    // Lock first
    std::array<uint8_t, 16> lock_data = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    (void)make_test_result(handler, header, lock_data);
    EXPECT_TRUE(handler.is_locked());

    // Unlock
    std::array<uint8_t, 16> unlock_data = {
        0x00,
        0x00,
        0x00,
        0x01,  // UNLOCK flag
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00};

    auto result = make_test_result(handler, header, unlock_data);
    EXPECT_EQ(result.status, AEM_STATUS_SUCCESS);
    EXPECT_FALSE(handler.is_locked());
}

TEST(nanoavb_entity_lock, timeout_expiration)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_LOCK_ENTITY);
    header.controller_entity_id = statusbar::ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

    std::array<uint8_t, 16> lock_data = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto now = sm::TimePoint{};
    handler.set_event_time(now);
    (void)make_test_result(handler, header, lock_data);
    EXPECT_TRUE(handler.is_locked());

    // Tick at 59 seconds - still locked
    auto before_timeout = now + std::chrono::seconds(59);
    handler.tick(before_timeout);
    EXPECT_TRUE(handler.is_locked());

    // Tick at 61 seconds - lock expired
    auto after_timeout = now + std::chrono::seconds(61);
    handler.tick(after_timeout);
    EXPECT_FALSE(handler.is_locked());
}

TEST(nanoavb_entity_lock, refresh_extends_timeout)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_LOCK_ENTITY);
    header.controller_entity_id = statusbar::ieee::Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

    std::array<uint8_t, 16> lock_data = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto now = sm::TimePoint{};
    handler.set_event_time(now);
    (void)make_test_result(handler, header, lock_data);
    EXPECT_TRUE(handler.is_locked());

    // Refresh at 30 seconds
    auto refresh_time = now + std::chrono::seconds(30);
    handler.set_event_time(refresh_time);
    (void)make_test_result(handler, header, lock_data);

    // Original timeout (60s) should have been extended
    auto past_original = now + std::chrono::seconds(61);
    handler.tick(past_original);
    EXPECT_TRUE(handler.is_locked());  // Still locked because refresh extended it

    // Now past the refreshed timeout (30 + 60 = 90s)
    auto past_refreshed = now + std::chrono::seconds(91);
    handler.tick(past_refreshed);
    EXPECT_FALSE(handler.is_locked());
}

//
// Get Configuration Tests
//
TEST(nanoavb_entity_config, get_configuration)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};
    handler.set_current_configuration(0);

    auto header = create_aem_header(AEM_COMMAND_GET_CONFIGURATION);
    auto result = make_test_result(handler, header, {});

    EXPECT_EQ(result.status, AEM_STATUS_SUCCESS);
    EXPECT_EQ(result.response_data().size(), 4);
    // Check configuration index is 0 (network byte order)
    EXPECT_EQ(result.response_data()[2], 0);
    EXPECT_EQ(result.response_data()[3], 0);
}

//
// Not Implemented Command Tests
//
TEST(nanoavb_entity_commands, unsupported_command)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_REBOOT);  // Not implemented
    auto result = make_test_result(handler, header, {});

    EXPECT_EQ(result.status, AEM_STATUS_NOT_IMPLEMENTED);
}

//
// Stub Command Tests
//
TEST(nanoavb_entity_stubs, set_control_not_implemented)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_SET_CONTROL);
    auto result = make_test_result(handler, header, {});

    EXPECT_EQ(result.status, AEM_STATUS_NOT_IMPLEMENTED);
}

TEST(nanoavb_entity_stubs, get_control_not_implemented)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_GET_CONTROL);
    auto result = make_test_result(handler, header, {});

    EXPECT_EQ(result.status, AEM_STATUS_NOT_IMPLEMENTED);
}

TEST(nanoavb_entity_stubs, get_counters_not_implemented)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    auto header = create_aem_header(AEM_COMMAND_GET_COUNTERS);
    auto result = make_test_result(handler, header, {});

    EXPECT_EQ(result.status, AEM_STATUS_NOT_IMPLEMENTED);
}

//
// AECP Packet Processing Tests
//
TEST(nanoavb_entity_aecp, process_packet_read_descriptor)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    // Track sent responses
    std::vector<uint8_t> captured_response;
    Eui48 captured_dest{};

    handler.set_callbacks(AemCommandHandlerCallbacks{.send_response = [&](Eui48 const& dest, std::span<uint8_t const> resp) {
        captured_dest = dest;
        captured_response.assign(resp.begin(), resp.end());
        return true;
    }});

    // Build AECP AEM READ_DESCRIPTOR command packet
    std::array<uint8_t, 32> packet{};
    AemDu cmd_header{};
    cmd_header.init_command(AEM_COMMAND_READ_DESCRIPTOR, 20);  // 12 + 8 command data
    cmd_header.target_entity_id = model.get_entity().entity_id;
    cmd_header.controller_entity_id = Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    span_copy(make_span(packet).first(AemDu::LENGTH), make_const_span(cmd_header));

    // Add READ_DESCRIPTOR command data (entity descriptor)
    packet[24] = 0x00;
    packet[25] = 0x00;  // Config index
    packet[26] = 0x00;
    packet[27] = 0x00;  // Reserved
    packet[28] = 0x00;
    packet[29] = 0x00;  // Descriptor type = ENTITY
    packet[30] = 0x00;
    packet[31] = 0x00;  // Descriptor index = 0

    Eui48 src_mac{0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    bool processed = handler.process_packet(src_mac, packet, model.get_entity().entity_id);

    EXPECT_TRUE(processed);
    EXPECT_EQ(captured_dest, src_mac);

    // Verify response was sent
    EXPECT_FALSE(captured_response.empty());

    // Verify response header
    AemDu resp_header{};
    span_copy(make_span(resp_header), std::span<uint8_t const>(captured_response).first(AemDu::LENGTH));
    EXPECT_TRUE(resp_header.is_response());
    EXPECT_EQ(resp_header.status(), AEM_STATUS_SUCCESS);
    EXPECT_EQ(resp_header.command_code(), AEM_COMMAND_READ_DESCRIPTOR);
}

TEST(nanoavb_entity_aecp, process_packet_wrong_target)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    bool callback_called = false;
    handler.set_callbacks(AemCommandHandlerCallbacks{.send_response = [&](Eui48 const&, std::span<uint8_t const>) {
        callback_called = true;
        return true;
    }});

    // Build packet with wrong target_entity_id
    std::array<uint8_t, 32> packet{};
    AemDu cmd_header{};
    cmd_header.init_command(AEM_COMMAND_READ_DESCRIPTOR, 20);
    cmd_header.target_entity_id = Eui64{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // Wrong ID
    cmd_header.controller_entity_id = Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    span_copy(make_span(packet).first(AemDu::LENGTH), make_const_span(cmd_header));

    Eui48 src_mac{0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    bool processed = handler.process_packet(src_mac, packet, model.get_entity().entity_id);

    EXPECT_FALSE(processed);  // Should be ignored
    EXPECT_FALSE(callback_called);
}

TEST(nanoavb_entity_aecp, process_packet_too_short)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    bool callback_called = false;
    handler.set_callbacks(AemCommandHandlerCallbacks{.send_response = [&](Eui48 const&, std::span<uint8_t const>) {
        callback_called = true;
        return true;
    }});

    // Packet too short to contain AemDu header
    std::array<uint8_t, 10> packet{};

    Eui48 src_mac{0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    bool processed = handler.process_packet(src_mac, packet, model.get_entity().entity_id);

    EXPECT_FALSE(processed);
    EXPECT_FALSE(callback_called);
}

TEST(nanoavb_entity_aecp, process_packet_invalid_header)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    bool callback_called = false;
    handler.set_callbacks(AemCommandHandlerCallbacks{.send_response = [&](Eui48 const&, std::span<uint8_t const>) {
        callback_called = true;
        return true;
    }});

    // Build packet with invalid subtype
    std::array<uint8_t, 32> packet{};
    AemDu cmd_header{};
    cmd_header.init_command(AEM_COMMAND_READ_DESCRIPTOR, 20);
    cmd_header.subtype = 0x00;  // Invalid - should be 0xFB for AECP
    cmd_header.target_entity_id = model.get_entity().entity_id;
    span_copy(make_span(packet).first(AemDu::LENGTH), make_const_span(cmd_header));

    Eui48 src_mac{0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    bool processed = handler.process_packet(src_mac, packet, model.get_entity().entity_id);

    EXPECT_FALSE(processed);
    EXPECT_FALSE(callback_called);
}

TEST(nanoavb_entity_aecp, process_packet_no_callback)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};

    // No callbacks set

    std::array<uint8_t, 32> packet{};
    AemDu cmd_header{};
    cmd_header.init_command(AEM_COMMAND_READ_DESCRIPTOR, 20);
    cmd_header.target_entity_id = model.get_entity().entity_id;
    span_copy(make_span(packet).first(AemDu::LENGTH), make_const_span(cmd_header));

    Eui48 src_mac{0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    bool processed = handler.process_packet(src_mac, packet, model.get_entity().entity_id);

    EXPECT_FALSE(processed);  // No callback to send response
}

// ===========================================================================
// release_acquisition / handle_controller_available / locking_controller
// ===========================================================================

TEST(nanoavb_entity_release, release_acquisition_clears_state)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};
    auto header = create_aem_header(AEM_COMMAND_ACQUIRE_ENTITY);
    header.controller_entity_id = Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    std::array<uint8_t, 16> data = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    (void)make_test_result(handler, header, data);
    EXPECT_TRUE(handler.is_acquired());
    handler.release_acquisition();
    EXPECT_FALSE(handler.is_acquired());
    EXPECT_TRUE(handler.acquiring_controller() == Eui64{});
}

TEST(nanoavb_entity_release, release_allows_new_acquire)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};
    auto ha = create_aem_header(AEM_COMMAND_ACQUIRE_ENTITY);
    ha.controller_entity_id = Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    std::array<uint8_t, 16> data = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    (void)make_test_result(handler, ha, data);
    handler.release_acquisition();
    auto hb = create_aem_header(AEM_COMMAND_ACQUIRE_ENTITY);
    hb.controller_entity_id = Eui64{0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
    auto r = make_test_result(handler, hb, data);
    EXPECT_EQ(r.status, AEM_STATUS_SUCCESS);
    EXPECT_TRUE(handler.acquiring_controller() == hb.controller_entity_id);
}

TEST(nanoavb_entity_controller_avail, returns_success)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};
    auto header = create_aem_header(AEM_COMMAND_CONTROLLER_AVAILABLE);
    auto result = make_test_result(handler, header, {});
    EXPECT_EQ(result.status, AEM_STATUS_SUCCESS);
    EXPECT_TRUE(result.response_data().empty());
}

TEST(nanoavb_entity_lock_ctrl, locking_controller_zero_when_unlocked)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};
    EXPECT_FALSE(handler.is_locked());
    EXPECT_TRUE(handler.locking_controller() == Eui64{});
}

TEST(nanoavb_entity_lock_ctrl, locking_controller_returns_locker)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};
    auto ctrl = Eui64{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
    auto header = create_aem_header(AEM_COMMAND_LOCK_ENTITY);
    header.controller_entity_id = ctrl;
    std::array<uint8_t, 16> data = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    handler.set_event_time(sm::TimePoint{});
    (void)make_test_result(handler, header, data);
    EXPECT_TRUE(handler.is_locked());
    EXPECT_TRUE(handler.locking_controller() == ctrl);
}

TEST(nanoavb_entity_lock_ctrl, release_lock_clears_state)
{
    auto model = create_test_model();
    AemCommandHandler handler{model};
    auto header = create_aem_header(AEM_COMMAND_LOCK_ENTITY);
    header.controller_entity_id = Eui64{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
    std::array<uint8_t, 16> data = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    handler.set_event_time(sm::TimePoint{});
    (void)make_test_result(handler, header, data);
    EXPECT_TRUE(handler.is_locked());
    handler.release_lock();
    EXPECT_FALSE(handler.is_locked());
    EXPECT_TRUE(handler.locking_controller() == Eui64{});
}

//
// Test Runner
//
int statusbar_nanoavb_nanoavb_entity_test(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    int result = 0;
    result |= ::statusbar::test::TestRegister::run_section("nanoavb_entity_test");
    return result;
}