#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// ControllerTuiApp — ATDECC controller TUI presentation layer.
///
/// Subscribes to ControllerEvents from the business layer (ControllerSimple)
/// via handle_event() and maintains its own UI state. The TUI emits
/// ControllerActions via take_pending_actions() which the main loop
/// dispatches to the business layer via ControllerSimple::dispatch().
///
/// No direct coupling to the network layer — only shared model types.

#include "statusbar/atdecc_tools/atdecc_controller_model.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/tui/tui.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace statusbar::atdecc_tools {

/// Interaction mode for the controller UI.
enum class ControllerMode
{
    Normal,                    ///< Browsing stream list
    SelectListener,            ///< Talker pending, waiting for listener selection
    DisconnectSelectListener,  ///< Talker pending for disconnect, waiting for listener
    EntityDetail,              ///< Browsing entity descriptors
};

/// Which pane currently has focus (cursor + key input).
enum class Focus : uint8_t
{
    Talkers,       ///< Left pane: talker stream sources
    Listeners,     ///< Middle pane: listener stream sinks
    Connections,   ///< Right pane: active connections
    EntityDetail,  ///< Right pane: entity descriptor detail
};

/// Kind of stream endpoint row.
enum class StreamRowKind : uint8_t
{
    Header,    ///< Entity name header (non-selectable)
    Talker,    ///< Talker stream source (selectable)
    Listener,  ///< Listener stream sink (selectable)
};

/// A single row in the stream list.
struct StreamRow
{
    StreamRowKind kind{StreamRowKind::Talker};
    ieee::Eui64 entity_id{};
    uint16_t stream_index{0};
    std::string entity_name;
    std::string stream_name;
    std::string format;
};

/// TUI presentation layer for the ATDECC controller.
class ControllerTuiApp
{
  public:
    ControllerTuiApp();

    /// Replace the entity list with a fresh snapshot. Rebuilds the flat
    /// stream row list (one row per talker source + one per listener sink).
    void update_entities(std::span<EntityDisplayInfo const> entities);

    /// Set the network interface name shown in the header.
    void set_interface_name(std::string_view name);

    /// Handle a controller event from the business layer.
    /// The event updates TUI state (connections, detail, status).
    void handle_event(ControllerEvent const& event);

    /// Set the status bar message directly (used for local-only messages
    /// like keybinding feedback).
    void set_status(std::string_view message);

    /// Handle a key press. Returns false when the user requests quit.
    auto handle_key(int key) -> bool;

    /// Render the full UI to the given terminal.
    void render(tui::TerminalTui& tui) const;

    // ---- state queries (for testing) ----

    [[nodiscard]] auto mode() const noexcept -> ControllerMode;
    [[nodiscard]] auto status_message() const noexcept -> std::string_view;
    [[nodiscard]] auto pending_request() const noexcept -> std::optional<StreamRequest>;
    [[nodiscard]] auto focus() const noexcept -> Focus;
    [[nodiscard]] auto talker_rows() const noexcept -> std::span<StreamRow const>;
    [[nodiscard]] auto listener_rows() const noexcept -> std::span<StreamRow const>;
    [[nodiscard]] auto talker_cursor() const noexcept -> size_t;
    [[nodiscard]] auto listener_cursor() const noexcept -> size_t;
    [[nodiscard]] auto connection_cursor() const noexcept -> size_t;
    [[nodiscard]] auto entity_detail() const noexcept -> EntityDetail const* { return entity_detail_ ? &*entity_detail_ : nullptr; }
    [[nodiscard]] auto detail_scroll() const noexcept -> size_t { return detail_scroll_; }
    [[nodiscard]] auto connection_count() const noexcept -> size_t { return connections_.size(); }

    /// Returns true if state changed since last render, resets the flag.
    auto needs_render() noexcept -> bool;

    /// Force a full redraw on the next needs_render() call.
    void force_redraw() noexcept { dirty_ = true; }

    /// Drain and return the list of actions the app wants dispatched.
    [[nodiscard]] auto take_pending_actions() -> std::vector<ControllerAction>;

  private:
    // Event handlers (called by handle_event based on variant)
    void on_connection_added(ActiveConnection conn);
    void on_connection_removed(ieee::Eui64 const& listener_entity_id, uint16_t listener_unique_id);
    void on_entity_detail_ready(EntityDetail detail);

    // Detail / selection helpers
    void set_entity_detail(EntityDetail detail);
    void clear_entity_detail();

    // Key handlers
    void cursor_up();
    void cursor_down();
    void focus_left();
    void focus_right();
    void begin_connect();
    void begin_disconnect();
    void confirm_selection();
    void cancel_selection();
    void rebuild_rows(std::span<EntityDisplayInfo const> entities);
    void disconnect_selected_connection();

    auto handle_key_normal(int key) -> bool;
    void handle_key_select_listener(int key);
    void handle_key_entity_detail(int key);
    void trigger_refresh();
    void open_entity_detail();
    void identify_from_cursor();
    void identify_current_entity();
    void stream_control(bool start);

    void connection_cursor_up();
    void connection_cursor_down();
    void try_focus_connections();

    // Rendering helpers
    void render_normal_layout(tui::TerminalTui& tui) const;
    void render_entity_detail_layout(tui::TerminalTui& tui) const;
    void render_header(tui::TerminalTui& tui, int talker_width, int listener_width, int conn_width) const;
    void render_lists(tui::TerminalTui& tui, int talker_width, int listener_width, int conn_width) const;
    void render_footer(tui::TerminalTui& tui) const;
    [[nodiscard]] auto build_action_bar() const -> std::string;
    [[nodiscard]] auto build_normal_action_bar() const -> std::string;

    void render_talker_cell(tui::TerminalTui& tui, size_t row_abs_idx, int width) const;
    void render_listener_cell(tui::TerminalTui& tui, size_t row_abs_idx, int width) const;
    void render_connection_cell(tui::TerminalTui& tui, size_t row_abs_idx, int width) const;
    [[nodiscard]] auto is_pending_talker_row(StreamRow const& r) const noexcept -> bool;

    std::vector<ControllerAction> pending_actions_;
    std::vector<StreamRow> talker_rows_;
    std::vector<StreamRow> listener_rows_;
    std::string interface_name_;
    std::string status_{"Ready"};
    ControllerMode mode_{ControllerMode::Normal};
    Focus focus_{Focus::Talkers};
    size_t talker_cursor_{0};
    size_t listener_cursor_{0};
    size_t connection_cursor_{0};
    std::optional<StreamRequest> pending_;
    std::vector<ActiveConnection> connections_;
    std::optional<EntityDetail> entity_detail_;
    size_t detail_scroll_{0};
    bool dirty_{true};
};

}  // namespace statusbar::atdecc_tools
