// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Unit tests for ControllerTuiApp TUI state machine (three-pane layout)

#include "statusbar/atdecc_tools/atdecc_controller_tui.hpp"

#include "statusbar/ieee/ieee.hpp"
#include "statusbar/test/test.hpp"
#include "statusbar/tui/tui.hpp"

#include <unistd.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace statusbar;
using namespace statusbar::atdecc_tools;
using namespace statusbar::ieee;
using namespace statusbar::tui;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

/// Test helper — counts occurrences of each action kind in a drained queue.
struct ActionCounts
{
    int discover_count{0};
    std::optional<StreamRequest> last_connect;
    std::optional<StreamRequest> last_disconnect;
};

auto count_actions(std::vector<ControllerAction> const& actions) -> ActionCounts
{
    ActionCounts c{};
    for (auto const& a : actions) {
        switch (a.kind) {
            case ControllerActionKind::DiscoverAll:
                ++c.discover_count;
                break;
            case ControllerActionKind::ConnectStream:
                c.last_connect = a.request;
                break;
            case ControllerActionKind::DisconnectStream:
                c.last_disconnect = a.request;
                break;
            default:
                break;
        }
    }
    return c;
}

/// Entity fixture produces (header rows are non-selectable, cursor skips them):
///   Talkers (5 rows, 3 selectable):
///     row 0: Header "Talker A"
///     row 1: T0 @ Talker A       (00:01:..:07)       <- initial cursor
///     row 2: Header "Both C"
///     row 3: T0 @ Both C         (AA:BB:..:11)
///     row 4: T1 @ Both C
///   Listeners (9 rows, 6 selectable):
///     row 0: Header "Listener B"
///     row 1: L0 @ Listener B     (00:11:..:77)       <- initial cursor
///     row 2: Header "Both C"
///     row 3: L0 @ Both C
///     row 4: Header "MultiListener D"
///     row 5: L0 @ MultiListener D (11:22:..:88)
///     row 6: L1 @ MultiListener D
///     row 7: L2 @ MultiListener D
///     row 8: L3 @ MultiListener D
auto make_test_entities() -> std::vector<EntityDisplayInfo>
{
    return {
        EntityDisplayInfo{
            .entity_id = Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07},
            .name = "Talker A",
            .talker_stream_sources = 1,
            .listener_stream_sinks = 0,
            .has_talker = true,
            .has_listener = false,
        },
        EntityDisplayInfo{
            .entity_id = Eui64{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77},
            .name = "Listener B",
            .talker_stream_sources = 0,
            .listener_stream_sinks = 1,
            .has_talker = false,
            .has_listener = true,
        },
        EntityDisplayInfo{
            .entity_id = Eui64{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11},
            .name = "Both C",
            .talker_stream_sources = 2,
            .listener_stream_sinks = 1,
            .has_talker = true,
            .has_listener = true,
        },
        EntityDisplayInfo{
            .entity_id = Eui64{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88},
            .name = "MultiListener D",
            .talker_stream_sources = 0,
            .listener_stream_sinks = 4,
            .has_talker = false,
            .has_listener = true,
        },
    };
}

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------

TEST(controller_app_state, initial_mode_is_normal)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    EXPECT_TRUE(app.mode() == ControllerMode::Normal);
}

TEST(controller_app_state, initial_focus_is_talkers)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    EXPECT_TRUE(app.focus() == Focus::Talkers);
}

TEST(controller_app_state, initial_cursors_are_zero_before_entities)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    EXPECT_EQ(app.talker_cursor(), size_t{0});
    EXPECT_EQ(app.listener_cursor(), size_t{0});
    EXPECT_EQ(app.connection_cursor(), size_t{0});
}

TEST(controller_app_state, initial_row_lists_are_empty)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    EXPECT_EQ(app.talker_rows().size(), size_t{0});
    EXPECT_EQ(app.listener_rows().size(), size_t{0});
}

// ---------------------------------------------------------------------------
// Row expansion from entities
// ---------------------------------------------------------------------------

TEST(controller_app_rows, flatten_with_headers_produces_5_talker_rows_and_9_listener_rows)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());
    EXPECT_EQ(app.talker_rows().size(), size_t{5});    // 2 headers + 3 streams
    EXPECT_EQ(app.listener_rows().size(), size_t{9});  // 3 headers + 6 streams
}

TEST(controller_app_rows, talker_row_order_with_headers)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());
    auto rows = app.talker_rows();
    EXPECT_TRUE(rows[0].kind == StreamRowKind::Header);  // "Talker A"
    EXPECT_TRUE(rows[1].kind == StreamRowKind::Talker);  // T0 @ Talker A
    EXPECT_EQ(rows[1].stream_index, uint16_t{0});
    EXPECT_TRUE(rows[2].kind == StreamRowKind::Header);  // "Both C"
    EXPECT_TRUE(rows[3].kind == StreamRowKind::Talker);  // T0 @ Both C
    EXPECT_EQ(rows[3].stream_index, uint16_t{0});
    EXPECT_TRUE(rows[4].kind == StreamRowKind::Talker);  // T1 @ Both C
    EXPECT_EQ(rows[4].stream_index, uint16_t{1});
}

TEST(controller_app_rows, listener_row_order_with_headers)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());
    auto rows = app.listener_rows();
    EXPECT_TRUE(rows[0].kind == StreamRowKind::Header);  // "Listener B"
    EXPECT_TRUE(rows[1].kind == StreamRowKind::Listener);
    EXPECT_EQ(rows[1].stream_index, uint16_t{0});
    EXPECT_TRUE(rows[2].kind == StreamRowKind::Header);  // "Both C"
    EXPECT_TRUE(rows[4].kind == StreamRowKind::Header);  // "MultiListener D"
    EXPECT_TRUE(rows[8].kind == StreamRowKind::Listener);
    EXPECT_EQ(rows[8].stream_index, uint16_t{3});
}

// ---------------------------------------------------------------------------
// Cursor movement (within active pane)
// ---------------------------------------------------------------------------

TEST(controller_app_cursor, initial_cursor_lands_on_first_stream_row)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());
    // Row 0 is "Talker A" header; first selectable stream is row 1
    EXPECT_EQ(app.talker_cursor(), size_t{1});
    // Row 0 is "Listener B" header; first selectable sink is row 1
    EXPECT_EQ(app.listener_cursor(), size_t{1});
}

TEST(controller_app_cursor, down_skips_header_rows)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    EXPECT_EQ(app.talker_cursor(), size_t{1});  // T0 @ Talker A
    app.handle_key(KEY_DOWN);
    // Should skip header at row 2 and land on T0 @ Both C at row 3
    EXPECT_EQ(app.talker_cursor(), size_t{3});
    app.handle_key('j');
    EXPECT_EQ(app.talker_cursor(), size_t{4});  // T1 @ Both C
}

TEST(controller_app_cursor, talker_cursor_stops_at_last_stream)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    for (int i = 0; i < 10; ++i) {
        app.handle_key(KEY_DOWN);
    }
    EXPECT_EQ(app.talker_cursor(), size_t{4});  // T1 @ Both C (last stream)
}

TEST(controller_app_cursor, cursor_up_stops_at_first_stream_row)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    app.handle_key(KEY_UP);
    EXPECT_EQ(app.talker_cursor(), size_t{1});  // Can't go above first stream row
}

TEST(controller_app_cursor, cursor_up_skips_header_rows)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    // Start at T0 @ Talker A (row 1), go down twice to T1 @ Both C (row 4)
    app.handle_key('j');
    app.handle_key('j');
    EXPECT_EQ(app.talker_cursor(), size_t{4});
    // Up should skip header at row 2 back to T0 @ Talker A
    app.handle_key('k');
    EXPECT_EQ(app.talker_cursor(), size_t{3});  // T0 @ Both C
    app.handle_key('k');
    EXPECT_EQ(app.talker_cursor(), size_t{1});  // back to T0 @ Talker A (skipped header)
}

TEST(controller_app_cursor, no_crash_with_empty_list)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.handle_key(KEY_UP);
    app.handle_key(KEY_DOWN);
    EXPECT_EQ(app.talker_cursor(), size_t{0});
}

// ---------------------------------------------------------------------------
// Pane focus switching (3-way: Talkers <-> Listeners <-> Connections)
// ---------------------------------------------------------------------------

TEST(controller_app_focus, right_from_talkers_moves_to_listeners)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    app.handle_key(KEY_RIGHT);
    EXPECT_TRUE(app.focus() == Focus::Listeners);
}

TEST(controller_app_focus, l_key_moves_right)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    app.handle_key('l');
    EXPECT_TRUE(app.focus() == Focus::Listeners);
}

TEST(controller_app_focus, right_from_listeners_moves_to_connections_when_present)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    ActiveConnection conn{
        .talker_entity_id = Eui64{1, 2, 3, 4, 5, 6, 7, 8},
        .listener_entity_id = Eui64{9, 10, 11, 12, 13, 14, 15, 16},
    };
    app.handle_event(atdecc_tools::ConnectionAddedEvent{conn});

    app.handle_key(KEY_RIGHT);  // to Listeners
    app.handle_key(KEY_RIGHT);  // to Connections
    EXPECT_TRUE(app.focus() == Focus::Connections);
}

TEST(controller_app_focus, right_from_listeners_no_op_when_no_connections)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    app.handle_key(KEY_RIGHT);  // to Listeners
    app.handle_key(KEY_RIGHT);  // no connections — stays
    EXPECT_TRUE(app.focus() == Focus::Listeners);
}

TEST(controller_app_focus, left_from_listeners_moves_to_talkers)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    app.handle_key('l');
    app.handle_key('h');
    EXPECT_TRUE(app.focus() == Focus::Talkers);
}

TEST(controller_app_focus, left_from_connections_moves_to_listeners)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    ActiveConnection conn{
        .talker_entity_id = Eui64{1, 2, 3, 4, 5, 6, 7, 8},
        .listener_entity_id = Eui64{9, 10, 11, 12, 13, 14, 15, 16},
    };
    app.handle_event(atdecc_tools::ConnectionAddedEvent{conn});

    app.handle_key('l');
    app.handle_key('l');
    EXPECT_TRUE(app.focus() == Focus::Connections);
    app.handle_key('h');
    EXPECT_TRUE(app.focus() == Focus::Listeners);
}

TEST(controller_app_focus, left_from_talkers_is_noop)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    app.handle_key('h');
    EXPECT_TRUE(app.focus() == Focus::Talkers);
}

TEST(controller_app_focus, down_in_listeners_moves_listener_cursor)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    app.handle_key('l');                          // focus on Listeners
    EXPECT_EQ(app.listener_cursor(), size_t{1});  // L0 @ Listener B (row 0 is header)
    app.handle_key(KEY_DOWN);
    // Skips header at row 2, lands on L0 @ Both C at row 3
    EXPECT_EQ(app.listener_cursor(), size_t{3});
    // Talker cursor unchanged (still on first stream)
    EXPECT_EQ(app.talker_cursor(), size_t{1});
}

TEST(controller_app_focus, focus_returns_to_talkers_when_last_connection_removed)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    ActiveConnection conn{
        .talker_entity_id = Eui64{1, 2, 3, 4, 5, 6, 7, 8},
        .listener_entity_id = Eui64{9, 10, 11, 12, 13, 14, 15, 16},
    };
    app.handle_event(atdecc_tools::ConnectionAddedEvent{conn});
    app.handle_key('l');
    app.handle_key('l');
    EXPECT_TRUE(app.focus() == Focus::Connections);

    app.handle_event(atdecc_tools::ConnectionRemovedEvent{conn.listener_entity_id, conn.listener_unique_id});
    EXPECT_TRUE(app.focus() == Focus::Talkers);
}

// ---------------------------------------------------------------------------
// Global keys
// ---------------------------------------------------------------------------

TEST(controller_app_keys, q_returns_false)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    EXPECT_FALSE(app.handle_key('q'));
}

TEST(controller_app_keys, r_calls_discover)
{
    ControllerTuiApp app{};
    app.handle_key('r');
    auto ta = count_actions(app.take_pending_actions());
    EXPECT_EQ(ta.discover_count, 1);
}

TEST(controller_app_keys, ctrl_l_forces_redraw)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());
    (void)app.needs_render();
    app.handle_key(KEY_CTRL_L);
    EXPECT_TRUE(app.needs_render());
}

// ---------------------------------------------------------------------------
// Connect flow: Talker pane -> C -> auto-focus Listener pane -> Enter
// ---------------------------------------------------------------------------

TEST(controller_app_connect, c_moves_focus_to_listeners_and_sets_pending)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    // Cursor on T0 @ Talker A
    app.handle_key('c');
    EXPECT_TRUE(app.mode() == ControllerMode::SelectListener);
    EXPECT_TRUE(app.focus() == Focus::Listeners);
    EXPECT_TRUE(app.pending_request().has_value());
    EXPECT_EQ(app.pending_request()->talker_unique_id, uint16_t{0});
}

TEST(controller_app_connect, c_in_listeners_pane_shows_error)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    app.handle_key('l');  // focus Listeners
    app.handle_key('c');
    EXPECT_TRUE(app.mode() == ControllerMode::Normal);
    EXPECT_FALSE(app.pending_request().has_value());
}

TEST(controller_app_connect, full_connect_flow)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    // T0 @ Talker A (row 1) — press C
    app.handle_key('c');
    EXPECT_TRUE(app.focus() == Focus::Listeners);
    EXPECT_EQ(app.listener_cursor(), size_t{1});  // L0 @ Listener B (row 1, row 0 is header)

    // Enter to confirm
    app.handle_key(KEY_ENTER);
    EXPECT_TRUE(app.mode() == ControllerMode::Normal);
    EXPECT_TRUE(app.focus() == Focus::Talkers);
    ta = count_actions(app.take_pending_actions());
    EXPECT_TRUE(ta.last_connect.has_value());

    auto entities = make_test_entities();
    EXPECT_EQ(ta.last_connect->talker_entity_id, entities[0].entity_id);
    EXPECT_EQ(ta.last_connect->talker_unique_id, uint16_t{0});
    EXPECT_EQ(ta.last_connect->listener_entity_id, entities[1].entity_id);
    EXPECT_EQ(ta.last_connect->listener_unique_id, uint16_t{0});
}

TEST(controller_app_connect, full_connect_flow_with_cursor_moves)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    // Start at T0 @ Talker A (row 1). j skips header, lands on T0 @ Both C (row 3).
    // Another j lands on T1 @ Both C (row 4).
    app.handle_key('j');
    app.handle_key('j');
    EXPECT_EQ(app.talker_cursor(), size_t{4});

    app.handle_key('c');
    EXPECT_TRUE(app.focus() == Focus::Listeners);
    // Listener cursor is at L0 @ Listener B (row 1)
    EXPECT_EQ(app.listener_cursor(), size_t{1});

    // Navigate to L2 @ MultiListener D:
    //   j: skip header row 2, land on L0 @ Both C (row 3)
    //   j: skip header row 4, land on L0 @ MultiListener D (row 5)
    //   j: L1 @ MultiListener D (row 6)
    //   j: L2 @ MultiListener D (row 7)
    for (int i = 0; i < 4; ++i) {
        app.handle_key('j');
    }
    EXPECT_EQ(app.listener_cursor(), size_t{7});

    app.handle_key(KEY_ENTER);
    ta = count_actions(app.take_pending_actions());
    EXPECT_TRUE(ta.last_connect.has_value());
    EXPECT_EQ(ta.last_connect->talker_unique_id, uint16_t{1});
    EXPECT_EQ(ta.last_connect->listener_unique_id, uint16_t{2});
}

TEST(controller_app_connect, escape_cancels_from_listener_pane)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    app.handle_key('c');
    EXPECT_TRUE(app.mode() == ControllerMode::SelectListener);
    app.handle_key(KEY_ESCAPE);
    EXPECT_TRUE(app.mode() == ControllerMode::Normal);
    EXPECT_TRUE(app.focus() == Focus::Talkers);
    EXPECT_FALSE(app.pending_request().has_value());
}

TEST(controller_app_connect, talker_departs_during_listener_selection)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    app.handle_key('c');
    EXPECT_TRUE(app.mode() == ControllerMode::SelectListener);

    // Remove Talker A from the entity list
    std::vector<EntityDisplayInfo> smaller = {
        EntityDisplayInfo{
            .entity_id = Eui64{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77},
            .name = "Listener B",
            .talker_stream_sources = 0,
            .listener_stream_sinks = 1,
            .has_talker = false,
            .has_listener = true,
        },
    };
    app.update_entities(smaller);
    EXPECT_TRUE(app.mode() == ControllerMode::Normal);
    EXPECT_TRUE(app.focus() == Focus::Talkers);
}

TEST(controller_app_connect, connect_with_empty_list_shows_error)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.handle_key('c');
    EXPECT_TRUE(app.mode() == ControllerMode::Normal);
}

// ---------------------------------------------------------------------------
// Disconnect flow
// ---------------------------------------------------------------------------

TEST(controller_app_disconnect, d_on_talker_moves_focus_to_listeners)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    app.handle_key('d');
    EXPECT_TRUE(app.mode() == ControllerMode::DisconnectSelectListener);
    EXPECT_TRUE(app.focus() == Focus::Listeners);
    EXPECT_TRUE(app.pending_request().has_value());
}

TEST(controller_app_disconnect, full_disconnect_flow_via_panes)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    app.handle_key('d');
    app.handle_key(KEY_ENTER);
    EXPECT_TRUE(app.mode() == ControllerMode::Normal);
    ta = count_actions(app.take_pending_actions());
    EXPECT_TRUE(ta.last_disconnect.has_value());
}

TEST(controller_app_disconnect, d_in_connections_pane_disconnects_selected)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    ActiveConnection conn{
        .talker_entity_id = Eui64{1, 2, 3, 4, 5, 6, 7, 8},
        .talker_unique_id = 3,
        .listener_entity_id = Eui64{9, 10, 11, 12, 13, 14, 15, 16},
        .listener_unique_id = 2,
    };
    app.handle_event(atdecc_tools::ConnectionAddedEvent{conn});
    app.handle_key('l');  // to Listeners
    app.handle_key('l');  // to Connections
    app.handle_key('d');

    ta = count_actions(app.take_pending_actions());
    EXPECT_TRUE(ta.last_disconnect.has_value());
    EXPECT_EQ(ta.last_disconnect->talker_unique_id, uint16_t{3});
    EXPECT_EQ(ta.last_disconnect->listener_unique_id, uint16_t{2});
}

// ---------------------------------------------------------------------------
// Active connection tracking
// ---------------------------------------------------------------------------

TEST(controller_app_connections, add_connection)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    ActiveConnection conn{
        .talker_entity_id = Eui64{1, 2, 3, 4, 5, 6, 7, 8},
        .listener_entity_id = Eui64{9, 10, 11, 12, 13, 14, 15, 16},
    };
    app.handle_event(atdecc_tools::ConnectionAddedEvent{conn});
    EXPECT_EQ(app.connection_count(), size_t{1});
}

TEST(controller_app_connections, remove_connection)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    ActiveConnection conn{
        .talker_entity_id = Eui64{1, 2, 3, 4, 5, 6, 7, 8},
        .listener_entity_id = Eui64{9, 10, 11, 12, 13, 14, 15, 16},
    };
    app.handle_event(atdecc_tools::ConnectionAddedEvent{conn});
    app.handle_event(atdecc_tools::ConnectionRemovedEvent{conn.listener_entity_id, conn.listener_unique_id});
    EXPECT_EQ(app.connection_count(), size_t{0});
}

TEST(controller_app_connections, duplicate_listener_replaces)
{
    ActionCounts ta;
    ControllerTuiApp app{};

    ActiveConnection first{
        .talker_entity_id = Eui64{1, 2, 3, 4, 5, 6, 7, 8},
        .listener_entity_id = Eui64{9, 10, 11, 12, 13, 14, 15, 16},
    };
    ActiveConnection replacement{
        .talker_entity_id = Eui64{11, 12, 13, 14, 15, 16, 17, 18},
        .listener_entity_id = Eui64{9, 10, 11, 12, 13, 14, 15, 16},
    };

    app.handle_event(atdecc_tools::ConnectionAddedEvent{first});
    app.handle_event(atdecc_tools::ConnectionAddedEvent{replacement});
    EXPECT_EQ(app.connection_count(), size_t{1});
}

// ---------------------------------------------------------------------------
// Rendering smoke test
// ---------------------------------------------------------------------------

TEST(controller_app_render, render_does_not_crash)
{
    ActionCounts ta;
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());
    app.set_interface_name("eth0");
    EXPECT_TRUE(app.needs_render());
    EXPECT_FALSE(app.needs_render());
}

// ---------------------------------------------------------------------------
// Rendering to pipe (verifies actual ANSI output without a real terminal)
// ---------------------------------------------------------------------------

namespace {
struct TuiPipe
{
    int read_fd{-1};
    int write_fd{-1};

    TuiPipe()
    {
        int fds[2]{};
        if (::pipe(fds) == 0) {
            read_fd = fds[0];
            write_fd = fds[1];
        }
    }

    ~TuiPipe()
    {
        if (read_fd >= 0) {
            ::close(read_fd);
        }
        if (write_fd >= 0) {
            ::close(write_fd);
        }
    }

    auto drain() -> std::string
    {
        std::string result;
        char buf[4096]{};
        while (true) {
            auto const n = ::read(read_fd, buf, sizeof(buf));
            if (n <= 0) {
                break;
            }
            result.append(buf, static_cast<size_t>(n));
        }
        return result;
    }
};
}  // namespace

TEST(controller_app_render, render_normal_contains_title)
{
    TuiPipe pipe;
    {
        TerminalTui tui{pipe.write_fd};
        ControllerTuiApp app{};
        app.update_entities(make_test_entities());
        app.set_interface_name("eth0");
        app.render(tui);
    }
    ::close(pipe.write_fd);
    pipe.write_fd = -1;
    auto output = pipe.drain();
    EXPECT_TRUE(output.find("ATDECC Controller") != std::string::npos);
}

TEST(controller_app_render, render_normal_contains_interface_name)
{
    TuiPipe pipe;
    {
        TerminalTui tui{pipe.write_fd};
        ControllerTuiApp app{};
        app.update_entities(make_test_entities());
        app.set_interface_name("en7");
        app.render(tui);
    }
    ::close(pipe.write_fd);
    pipe.write_fd = -1;
    auto output = pipe.drain();
    EXPECT_TRUE(output.find("en7") != std::string::npos);
}

TEST(controller_app_render, render_normal_shows_entity_names)
{
    TuiPipe pipe;
    {
        TerminalTui tui{pipe.write_fd};
        ControllerTuiApp app{};
        app.update_entities(make_test_entities());
        app.render(tui);
    }
    ::close(pipe.write_fd);
    pipe.write_fd = -1;
    auto output = pipe.drain();
    EXPECT_TRUE(output.find("Talker A") != std::string::npos);
    EXPECT_TRUE(output.find("Listener B") != std::string::npos);
}

TEST(controller_app_render, render_shows_status_message)
{
    TuiPipe pipe;
    {
        TerminalTui tui{pipe.write_fd};
        ControllerTuiApp app{};
        app.set_status("Test status message");
        app.render(tui);
    }
    ::close(pipe.write_fd);
    pipe.write_fd = -1;
    auto output = pipe.drain();
    EXPECT_TRUE(output.find("Test status message") != std::string::npos);
}

TEST(controller_app_render, render_shows_action_bar)
{
    TuiPipe pipe;
    {
        TerminalTui tui{pipe.write_fd};
        ControllerTuiApp app{};
        app.update_entities(make_test_entities());
        app.render(tui);
    }
    ::close(pipe.write_fd);
    pipe.write_fd = -1;
    auto output = pipe.drain();
    // Normal mode, Talkers focus shows [C]onnect
    EXPECT_TRUE(output.find("[C]onnect") != std::string::npos);
}

TEST(controller_app_render, render_select_listener_shows_action_bar)
{
    TuiPipe pipe;
    {
        TerminalTui tui{pipe.write_fd};
        ControllerTuiApp app{};
        app.update_entities(make_test_entities());
        app.handle_key('c');  // enter SelectListener mode
        app.render(tui);
    }
    ::close(pipe.write_fd);
    pipe.write_fd = -1;
    auto output = pipe.drain();
    EXPECT_TRUE(output.find("Select LISTENER") != std::string::npos);
}

TEST(controller_app_render, render_connections_pane_shows_connection)
{
    TuiPipe pipe;
    {
        TerminalTui tui{pipe.write_fd};
        ControllerTuiApp app{};
        app.update_entities(make_test_entities());
        ActiveConnection conn{
            .talker_entity_id = Eui64{1, 2, 3, 4, 5, 6, 7, 8},
            .talker_unique_id = 0,
            .listener_entity_id = Eui64{9, 10, 11, 12, 13, 14, 15, 16},
            .listener_unique_id = 0,
            .talker_name = "MyTalker",
            .listener_name = "MyListener",
        };
        app.handle_event(atdecc_tools::ConnectionAddedEvent{conn});
        app.render(tui);
    }
    ::close(pipe.write_fd);
    pipe.write_fd = -1;
    auto output = pipe.drain();
    EXPECT_TRUE(output.find("MyTalker") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Entity detail mode
// ---------------------------------------------------------------------------

TEST(controller_app_detail, enter_opens_detail_mode)
{
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    // Press Enter on a talker stream row
    app.handle_key(KEY_ENTER);
    EXPECT_TRUE(app.mode() == ControllerMode::EntityDetail);
    EXPECT_TRUE(app.focus() == Focus::EntityDetail);
    // Should generate a ReadEntityDescriptors action
    auto actions = app.take_pending_actions();
    EXPECT_TRUE(!actions.empty());
    EXPECT_TRUE(actions[0].kind == ControllerActionKind::ReadEntityDescriptors);
}

TEST(controller_app_detail, entity_detail_event_sets_detail)
{
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());
    app.handle_key(KEY_ENTER);

    EntityDetail detail{
        .entity_id = Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07},
        .name = "Detail Entity",
        .lines = {{.text = "Line 1", .bold = true}, {.text = "Line 2"}, {.text = "Line 3"}},
    };
    app.handle_event(EntityDetailReadyEvent{detail});
    EXPECT_TRUE(app.entity_detail() != nullptr);
    EXPECT_EQ(app.entity_detail()->name, "Detail Entity");
    EXPECT_EQ(app.detail_scroll(), size_t{0});
}

TEST(controller_app_detail, detail_scroll_down)
{
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());
    app.handle_key(KEY_ENTER);

    EntityDetail detail{
        .entity_id = Eui64{},
        .name = "Test",
        .lines = {{.text = "L0"}, {.text = "L1"}, {.text = "L2"}, {.text = "L3"}},
    };
    app.handle_event(EntityDetailReadyEvent{detail});

    app.handle_key('j');
    EXPECT_EQ(app.detail_scroll(), size_t{1});
    app.handle_key(KEY_DOWN);
    EXPECT_EQ(app.detail_scroll(), size_t{2});
}

TEST(controller_app_detail, detail_scroll_up)
{
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());
    app.handle_key(KEY_ENTER);

    EntityDetail detail{
        .entity_id = Eui64{},
        .name = "Test",
        .lines = {{.text = "L0"}, {.text = "L1"}, {.text = "L2"}},
    };
    app.handle_event(EntityDetailReadyEvent{detail});

    app.handle_key('j');
    app.handle_key('j');
    EXPECT_EQ(app.detail_scroll(), size_t{2});
    app.handle_key('k');
    EXPECT_EQ(app.detail_scroll(), size_t{1});
}

TEST(controller_app_detail, escape_exits_detail)
{
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());
    app.handle_key(KEY_ENTER);

    EntityDetail detail{.entity_id = Eui64{}, .name = "Test", .lines = {}};
    app.handle_event(EntityDetailReadyEvent{detail});

    app.handle_key(KEY_ESCAPE);
    EXPECT_TRUE(app.mode() == ControllerMode::Normal);
    EXPECT_TRUE(app.focus() == Focus::Talkers);
    EXPECT_TRUE(app.entity_detail() == nullptr);
}

TEST(controller_app_detail, render_detail_layout_contains_detail)
{
    TuiPipe pipe;
    {
        TerminalTui tui{pipe.write_fd};
        ControllerTuiApp app{};
        app.update_entities(make_test_entities());
        app.handle_key(KEY_ENTER);

        EntityDetail detail{
            .entity_id = Eui64{},
            .name = "DetailTarget",
            .lines = {{.text = "Descriptor info line", .bold = false}},
        };
        app.handle_event(EntityDetailReadyEvent{detail});
        app.render(tui);
    }
    ::close(pipe.write_fd);
    pipe.write_fd = -1;
    auto output = pipe.drain();
    EXPECT_TRUE(output.find("DetailTarget") != std::string::npos);
    EXPECT_TRUE(output.find("Descriptor info line") != std::string::npos);
}

TEST(controller_app_detail, r_in_detail_refreshes_descriptors)
{
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());
    app.handle_key(KEY_ENTER);

    EntityDetail detail{
        .entity_id = Eui64{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11},
        .name = "Test",
        .lines = {},
    };
    app.handle_event(EntityDetailReadyEvent{detail});
    (void)app.take_pending_actions();  // clear any previous

    app.handle_key('r');
    auto actions = app.take_pending_actions();
    EXPECT_TRUE(!actions.empty());
    EXPECT_TRUE(actions[0].kind == ControllerActionKind::ReadEntityDescriptors);
    EXPECT_EQ(actions[0].request.talker_entity_id, detail.entity_id);
}

TEST(controller_app_detail, i_in_detail_identifies_entity)
{
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());
    app.handle_key(KEY_ENTER);

    EntityDetail detail{
        .entity_id = Eui64{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88},
        .name = "IdTarget",
        .lines = {},
    };
    app.handle_event(EntityDetailReadyEvent{detail});
    (void)app.take_pending_actions();

    app.handle_key('i');
    auto actions = app.take_pending_actions();
    EXPECT_TRUE(!actions.empty());
    EXPECT_TRUE(actions[0].kind == ControllerActionKind::IdentifyEntity);
}

// ---------------------------------------------------------------------------
// Additional key handling: identify, stream control
// ---------------------------------------------------------------------------

TEST(controller_app_keys, i_identifies_entity)
{
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());
    (void)app.take_pending_actions();

    app.handle_key('i');
    auto actions = app.take_pending_actions();
    EXPECT_TRUE(!actions.empty());
    EXPECT_TRUE(actions[0].kind == ControllerActionKind::IdentifyEntity);
}

TEST(controller_app_keys, s_starts_streaming)
{
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());
    (void)app.take_pending_actions();

    app.handle_key('s');
    auto actions = app.take_pending_actions();
    EXPECT_TRUE(!actions.empty());
    EXPECT_TRUE(actions[0].kind == ControllerActionKind::StartStreaming);
}

TEST(controller_app_keys, S_stops_streaming)
{
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());
    (void)app.take_pending_actions();

    app.handle_key('S');
    auto actions = app.take_pending_actions();
    EXPECT_TRUE(!actions.empty());
    EXPECT_TRUE(actions[0].kind == ControllerActionKind::StopStreaming);
}

// ---------------------------------------------------------------------------
// Connection cursor navigation
// ---------------------------------------------------------------------------

TEST(controller_app_conn_cursor, connection_cursor_up_down)
{
    ControllerTuiApp app{};
    app.update_entities(make_test_entities());

    ActiveConnection c1{
        .talker_entity_id = Eui64{1, 2, 3, 4, 5, 6, 7, 8},
        .talker_unique_id = 0,
        .listener_entity_id = Eui64{9, 10, 11, 12, 13, 14, 15, 16},
        .listener_unique_id = 0,
    };
    ActiveConnection c2{
        .talker_entity_id = Eui64{1, 2, 3, 4, 5, 6, 7, 8},
        .talker_unique_id = 1,
        .listener_entity_id = Eui64{9, 10, 11, 12, 13, 14, 15, 16},
        .listener_unique_id = 1,
    };
    app.handle_event(ConnectionAddedEvent{c1});
    app.handle_event(ConnectionAddedEvent{c2});

    // Navigate to connections pane
    app.handle_key('l');  // to Listeners
    app.handle_key('l');  // to Connections
    EXPECT_TRUE(app.focus() == Focus::Connections);
    EXPECT_EQ(app.connection_cursor(), size_t{0});

    app.handle_key(KEY_DOWN);
    EXPECT_EQ(app.connection_cursor(), size_t{1});

    app.handle_key(KEY_DOWN);
    EXPECT_EQ(app.connection_cursor(), size_t{1});  // stays at last

    app.handle_key(KEY_UP);
    EXPECT_EQ(app.connection_cursor(), size_t{0});

    app.handle_key(KEY_UP);
    EXPECT_EQ(app.connection_cursor(), size_t{0});  // stays at first
}

// ---------------------------------------------------------------------------
// Status event handling
// ---------------------------------------------------------------------------

TEST(controller_app_events, status_changed_event)
{
    ControllerTuiApp app{};
    app.handle_event(StatusChangedEvent{.status = "Network ready"});
    EXPECT_TRUE(app.status_message() == "Network ready");
}

TEST(controller_app_events, set_status_directly)
{
    ControllerTuiApp app{};
    app.set_status("Custom status");
    EXPECT_TRUE(app.status_message() == "Custom status");
    EXPECT_TRUE(app.needs_render());
}

TEST(controller_app_events, force_redraw)
{
    ControllerTuiApp app{};
    (void)app.needs_render();  // clear dirty flag
    app.force_redraw();
    EXPECT_TRUE(app.needs_render());
}

// ---------------------------------------------------------------------------
// Stream name and format display
// ---------------------------------------------------------------------------

TEST(controller_app_rows, stream_names_and_formats_in_rows)
{
    ControllerTuiApp app{};
    std::vector<EntityDisplayInfo> entities = {
        EntityDisplayInfo{
            .entity_id = Eui64{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07},
            .name = "NamedEntity",
            .talker_stream_sources = 1,
            .listener_stream_sinks = 1,
            .has_talker = true,
            .has_listener = true,
            .talker_stream_formats = {"AAF 48k 2ch"},
            .listener_stream_formats = {"AM824 48k 8ch"},
            .talker_stream_names = {"Stream Out 1"},
            .listener_stream_names = {"Stream In 1"},
        },
    };
    app.update_entities(entities);
    auto const& t_rows = app.talker_rows();
    auto const& l_rows = app.listener_rows();

    // Row 0 is header, Row 1 is the stream
    EXPECT_EQ(t_rows[1].stream_name, "Stream Out 1");
    EXPECT_EQ(t_rows[1].format, "AAF 48k 2ch");
    EXPECT_EQ(l_rows[1].stream_name, "Stream In 1");
    EXPECT_EQ(l_rows[1].format, "AM824 48k 8ch");
}

TEST_MAIN(statusbar_atdecc_tools, atdecc_controller_tui_test)
