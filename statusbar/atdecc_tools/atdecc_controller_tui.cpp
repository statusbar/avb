// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/atdecc_tools/atdecc_controller_tui.hpp"

#include "statusbar/ieee/ieee.hpp"
#include "statusbar/tui/tui.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace statusbar::atdecc_tools {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ControllerTuiApp::ControllerTuiApp() = default;

auto ControllerTuiApp::take_pending_actions() -> std::vector<ControllerAction>
{
    std::vector<ControllerAction> out;
    out.swap(pending_actions_);
    return out;
}

// ---------------------------------------------------------------------------
// Public mutators
// ---------------------------------------------------------------------------

void ControllerTuiApp::rebuild_rows(std::span<EntityDisplayInfo const> entities)
{
    talker_rows_.clear();
    listener_rows_.clear();
    for (auto const& e : entities) {
        std::string const name = e.name.empty() ? ieee::to_string(e.entity_id) : e.name;
        if (e.has_talker && e.talker_stream_sources > 0) {
            StreamRow header{.kind = StreamRowKind::Header, .entity_id = e.entity_id, .stream_index = 0, .entity_name = name};
            talker_rows_.push_back(std::move(header));
            for (uint16_t i = 0; i < e.talker_stream_sources; ++i) {
                StreamRow row{.kind = StreamRowKind::Talker, .entity_id = e.entity_id, .stream_index = i, .entity_name = name};
                if (i < e.talker_stream_names.size()) {
                    row.stream_name = e.talker_stream_names[i];
                }
                if (i < e.talker_stream_formats.size()) {
                    row.format = e.talker_stream_formats[i];
                }
                talker_rows_.push_back(std::move(row));
            }
        }
        if (e.has_listener && e.listener_stream_sinks > 0) {
            StreamRow header{.kind = StreamRowKind::Header, .entity_id = e.entity_id, .stream_index = 0, .entity_name = name};
            listener_rows_.push_back(std::move(header));
            for (uint16_t i = 0; i < e.listener_stream_sinks; ++i) {
                StreamRow row{.kind = StreamRowKind::Listener, .entity_id = e.entity_id, .stream_index = i, .entity_name = name};
                if (i < e.listener_stream_names.size()) {
                    row.stream_name = e.listener_stream_names[i];
                }
                if (i < e.listener_stream_formats.size()) {
                    row.format = e.listener_stream_formats[i];
                }
                listener_rows_.push_back(std::move(row));
            }
        }
    }

    // If the cursor lands on a header, move it to the first stream row after it
    auto skip_to_stream = [](std::vector<StreamRow> const& rows, size_t& cur) {
        while (cur < rows.size() && rows[cur].kind == StreamRowKind::Header) {
            ++cur;
        }
        if (cur >= rows.size() && !rows.empty()) {
            cur = rows.size() - 1;
            while (cur > 0 && rows[cur].kind == StreamRowKind::Header) {
                --cur;
            }
        }
    };
    skip_to_stream(talker_rows_, talker_cursor_);
    skip_to_stream(listener_rows_, listener_cursor_);
}

void ControllerTuiApp::update_entities(std::span<EntityDisplayInfo const> entities)
{
    rebuild_rows(entities);

    // Refresh talker_format on each active connection from the updated entity data
    for (auto& conn : connections_) {
        for (auto const& e : entities) {
            if (e.entity_id == conn.talker_entity_id && conn.talker_unique_id < e.talker_stream_formats.size()) {
                conn.talker_format = e.talker_stream_formats[conn.talker_unique_id];
                break;
            }
        }
    }

    // Clamp cursors
    if (!talker_rows_.empty() && talker_cursor_ >= talker_rows_.size()) {
        talker_cursor_ = talker_rows_.size() - 1;
    } else if (talker_rows_.empty()) {
        talker_cursor_ = 0;
    }
    if (!listener_rows_.empty() && listener_cursor_ >= listener_rows_.size()) {
        listener_cursor_ = listener_rows_.size() - 1;
    } else if (listener_rows_.empty()) {
        listener_cursor_ = 0;
    }

    // Cancel pending selection if the pending talker has departed
    if (pending_.has_value() && mode_ != ControllerMode::Normal) {
        bool talker_found = false;
        for (auto const& r : talker_rows_) {
            if (r.kind == StreamRowKind::Talker && r.entity_id == pending_->talker_entity_id &&
                r.stream_index == pending_->talker_unique_id) {
                talker_found = true;
                break;
            }
        }
        if (!talker_found) {
            mode_ = ControllerMode::Normal;
            pending_ = std::nullopt;
            focus_ = Focus::Talkers;
            status_ = "Talker departed — selection cancelled";
        }
    }

    dirty_ = true;
}

void ControllerTuiApp::set_interface_name(std::string_view name)
{
    interface_name_ = std::string{name};
    dirty_ = true;
}

void ControllerTuiApp::set_status(std::string_view message)
{
    status_ = std::string{message};
    dirty_ = true;
}

// ---------------------------------------------------------------------------
// Key handling
// ---------------------------------------------------------------------------

auto ControllerTuiApp::handle_key(int key) -> bool
{
    // Ctrl-L: force full redraw regardless of mode
    if (key == tui::KEY_CTRL_L) {
        dirty_ = true;
        return true;
    }

    bool keep_running = true;
    switch (mode_) {
        case ControllerMode::Normal:
            keep_running = handle_key_normal(key);
            break;
        case ControllerMode::SelectListener:
        case ControllerMode::DisconnectSelectListener:
            handle_key_select_listener(key);
            break;
        case ControllerMode::EntityDetail:
            handle_key_entity_detail(key);
            break;
    }

    dirty_ = true;
    return keep_running;
}

auto ControllerTuiApp::handle_key_normal(int key) -> bool
{
    if (key == tui::KEY_UP || key == 'k') {
        cursor_up();
    } else if (key == tui::KEY_DOWN || key == 'j') {
        cursor_down();
    } else if (key == tui::KEY_LEFT || key == 'h') {
        focus_left();
    } else if (key == tui::KEY_RIGHT || key == 'l') {
        focus_right();
    } else if (key == tui::KEY_ENTER) {
        open_entity_detail();
    } else if (key == 'c' || key == 'C') {
        begin_connect();
    } else if (key == 'd' || key == 'D') {
        begin_disconnect();
    } else if (key == 'r' || key == 'R') {
        trigger_refresh();
    } else if (key == 'i') {
        identify_from_cursor();
    } else if (key == 's') {
        stream_control(true);
    } else if (key == 'S') {
        stream_control(false);
    } else if (key == 'q' || key == 'Q') {
        return false;
    }
    return true;
}

void ControllerTuiApp::handle_key_select_listener(int key)
{
    if (key == tui::KEY_UP || key == 'k') {
        cursor_up();
    } else if (key == tui::KEY_DOWN || key == 'j') {
        cursor_down();
    } else if (key == tui::KEY_ENTER) {
        confirm_selection();
    } else if (key == tui::KEY_ESCAPE) {
        cancel_selection();
    }
}

void ControllerTuiApp::trigger_refresh()
{
    pending_actions_.push_back(ControllerAction{.kind = ControllerActionKind::DiscoverAll});
    set_status("Discovering...");
}

void ControllerTuiApp::handle_key_entity_detail(int key)
{
    if (key == tui::KEY_UP || key == 'k') {
        if (detail_scroll_ > 0) {
            --detail_scroll_;
        }
    } else if (key == tui::KEY_DOWN || key == 'j') {
        if (entity_detail_ && detail_scroll_ < entity_detail_->lines.size()) {
            ++detail_scroll_;
        }
    } else if (key == tui::KEY_ESCAPE) {
        clear_entity_detail();
    } else if (key == 'r' || key == 'R') {
        if (entity_detail_) {
            StreamRequest req{};
            req.talker_entity_id = entity_detail_->entity_id;
            pending_actions_.push_back(ControllerAction{.kind = ControllerActionKind::ReadEntityDescriptors, .request = req});
            set_status("Refreshing descriptors...");
        }
    } else if (key == 'i') {
        identify_current_entity();
    }
}

void ControllerTuiApp::open_entity_detail()
{
    auto const& rows = (focus_ == Focus::Talkers) ? talker_rows_ : listener_rows_;
    size_t const cursor = (focus_ == Focus::Talkers) ? talker_cursor_ : listener_cursor_;
    if (focus_ != Focus::Talkers && focus_ != Focus::Listeners) {
        return;
    }
    if (cursor >= rows.size() || rows[cursor].kind == StreamRowKind::Header) {
        return;
    }
    auto const& row = rows[cursor];
    StreamRequest req{};
    req.talker_entity_id = row.entity_id;
    pending_actions_.push_back(ControllerAction{.kind = ControllerActionKind::ReadEntityDescriptors, .request = req});
    mode_ = ControllerMode::EntityDetail;
    focus_ = Focus::EntityDetail;
    set_status(std::format("Loading descriptors for {}...", row.entity_name));
}

void ControllerTuiApp::set_entity_detail(EntityDetail detail)
{
    entity_detail_ = std::move(detail);
    detail_scroll_ = 0;
    mode_ = ControllerMode::EntityDetail;
    focus_ = Focus::EntityDetail;
    dirty_ = true;
}

void ControllerTuiApp::clear_entity_detail()
{
    entity_detail_ = std::nullopt;
    detail_scroll_ = 0;
    mode_ = ControllerMode::Normal;
    focus_ = Focus::Talkers;
    dirty_ = true;
}

void ControllerTuiApp::identify_current_entity()
{
    if (entity_detail_) {
        StreamRequest req{};
        req.talker_entity_id = entity_detail_->entity_id;
        pending_actions_.push_back(ControllerAction{.kind = ControllerActionKind::IdentifyEntity, .request = req});
        set_status("Identify sent");
        return;
    }
    identify_from_cursor();
}

void ControllerTuiApp::identify_from_cursor()
{
    auto const& rows = (focus_ == Focus::Talkers) ? talker_rows_ : (focus_ == Focus::Listeners) ? listener_rows_ : talker_rows_;
    size_t const cursor = (focus_ == Focus::Talkers) ? talker_cursor_ : (focus_ == Focus::Listeners) ? listener_cursor_ : 0;
    if (focus_ != Focus::Talkers && focus_ != Focus::Listeners) {
        return;
    }
    if (cursor >= rows.size() || rows[cursor].kind == StreamRowKind::Header) {
        return;
    }
    StreamRequest req{};
    req.talker_entity_id = rows[cursor].entity_id;
    pending_actions_.push_back(ControllerAction{.kind = ControllerActionKind::IdentifyEntity, .request = req});
    set_status("Identify sent");
}

void ControllerTuiApp::stream_control(bool start)
{
    auto const& rows = (focus_ == Focus::Talkers) ? talker_rows_ : (focus_ == Focus::Listeners) ? listener_rows_ : talker_rows_;
    size_t const cursor = (focus_ == Focus::Talkers) ? talker_cursor_ : (focus_ == Focus::Listeners) ? listener_cursor_ : 0;
    if (focus_ != Focus::Talkers && focus_ != Focus::Listeners) {
        return;
    }
    if (cursor >= rows.size() || rows[cursor].kind == StreamRowKind::Header) {
        return;
    }
    auto const& row = rows[cursor];
    StreamRequest req{};
    req.talker_entity_id = row.entity_id;
    req.desc_type = (row.kind == StreamRowKind::Talker) ? uint16_t{0x0006} : uint16_t{0x0005};  // STREAM_OUTPUT : STREAM_INPUT
    req.desc_index = row.stream_index;
    auto const kind = start ? ControllerActionKind::StartStreaming : ControllerActionKind::StopStreaming;
    pending_actions_.push_back(ControllerAction{.kind = kind, .request = req});
    set_status(std::format("{} stream {}:{}...", start ? "Starting" : "Stopping", row.entity_name, row.stream_index));
}

// ---------------------------------------------------------------------------
// State queries
// ---------------------------------------------------------------------------

auto ControllerTuiApp::mode() const noexcept -> ControllerMode
{
    return mode_;
}

auto ControllerTuiApp::status_message() const noexcept -> std::string_view
{
    return status_;
}

auto ControllerTuiApp::pending_request() const noexcept -> std::optional<StreamRequest>
{
    return pending_;
}

auto ControllerTuiApp::focus() const noexcept -> Focus
{
    return focus_;
}

auto ControllerTuiApp::talker_rows() const noexcept -> std::span<StreamRow const>
{
    return talker_rows_;
}

auto ControllerTuiApp::listener_rows() const noexcept -> std::span<StreamRow const>
{
    return listener_rows_;
}

auto ControllerTuiApp::talker_cursor() const noexcept -> size_t
{
    return talker_cursor_;
}

auto ControllerTuiApp::listener_cursor() const noexcept -> size_t
{
    return listener_cursor_;
}

auto ControllerTuiApp::connection_cursor() const noexcept -> size_t
{
    return connection_cursor_;
}

auto ControllerTuiApp::needs_render() noexcept -> bool
{
    bool const was_dirty = dirty_;
    dirty_ = false;
    return was_dirty;
}

void ControllerTuiApp::handle_event(ControllerEvent const& event)
{
    std::visit(
        [this](auto const& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, ConnectionAddedEvent>) {
                on_connection_added(e.connection);
            } else if constexpr (std::is_same_v<T, ConnectionRemovedEvent>) {
                on_connection_removed(e.listener_entity_id, e.listener_unique_id);
            } else if constexpr (std::is_same_v<T, EntityDetailReadyEvent>) {
                on_entity_detail_ready(e.detail);
            } else if constexpr (std::is_same_v<T, StatusChangedEvent>) {
                set_status(e.status);
            }
        },
        event);
}

void ControllerTuiApp::on_connection_added(ActiveConnection conn)
{
    // Fill in talker_format from the current talker_rows_ cache if available
    if (conn.talker_format.empty()) {
        for (auto const& r : talker_rows_) {
            if (r.kind == StreamRowKind::Talker && r.entity_id == conn.talker_entity_id &&
                r.stream_index == conn.talker_unique_id) {
                conn.talker_format = r.format;
                break;
            }
        }
    }
    on_connection_removed(conn.listener_entity_id, conn.listener_unique_id);
    auto it = std::lower_bound(connections_.begin(), connections_.end(), conn);
    connections_.insert(it, std::move(conn));
    dirty_ = true;
}

void ControllerTuiApp::on_connection_removed(ieee::Eui64 const& listener_entity_id, uint16_t listener_unique_id)
{
    auto it = std::remove_if(connections_.begin(), connections_.end(), [&](ActiveConnection const& c) {
        return c.listener_entity_id == listener_entity_id && c.listener_unique_id == listener_unique_id;
    });
    if (it != connections_.end()) {
        connections_.erase(it, connections_.end());
        dirty_ = true;
    }

    // Keep connection cursor in bounds; move focus off if list empty
    if (connections_.empty()) {
        connection_cursor_ = 0;
        if (focus_ == Focus::Connections) {
            focus_ = Focus::Talkers;
        }
    } else if (connection_cursor_ >= connections_.size()) {
        connection_cursor_ = connections_.size() - 1;
    }
}

void ControllerTuiApp::on_entity_detail_ready(EntityDetail detail)
{
    set_status(std::format("Loaded descriptors for {}", detail.name));
    set_entity_detail(std::move(detail));
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

namespace {
/// Move a cursor up, skipping header rows. Stays put if no stream row is above.
void cursor_up_skip_headers(std::vector<StreamRow> const& rows, size_t& cursor)
{
    if (rows.empty() || cursor == 0) {
        return;
    }
    size_t c = cursor;
    while (c > 0) {
        --c;
        if (rows[c].kind != StreamRowKind::Header) {
            cursor = c;
            return;
        }
    }
    // No stream row found above — leave cursor where it was
}

/// Move a cursor down, skipping header rows. Stays put if no stream row is below.
void cursor_down_skip_headers(std::vector<StreamRow> const& rows, size_t& cursor)
{
    if (rows.empty()) {
        return;
    }
    for (size_t c = cursor + 1; c < rows.size(); ++c) {
        if (rows[c].kind != StreamRowKind::Header) {
            cursor = c;
            return;
        }
    }
}
}  // namespace

void ControllerTuiApp::connection_cursor_up()
{
    if (!connections_.empty() && connection_cursor_ > 0) {
        --connection_cursor_;
    }
}

void ControllerTuiApp::connection_cursor_down()
{
    if (!connections_.empty() && connection_cursor_ + 1 < connections_.size()) {
        ++connection_cursor_;
    }
}

void ControllerTuiApp::cursor_up()
{
    switch (focus_) {
        case Focus::Talkers:
            cursor_up_skip_headers(talker_rows_, talker_cursor_);
            break;
        case Focus::Listeners:
            cursor_up_skip_headers(listener_rows_, listener_cursor_);
            break;
        case Focus::Connections:
            connection_cursor_up();
            break;
        case Focus::EntityDetail:
            break;  // handled in handle_key_entity_detail
    }
}

void ControllerTuiApp::cursor_down()
{
    switch (focus_) {
        case Focus::Talkers:
            cursor_down_skip_headers(talker_rows_, talker_cursor_);
            break;
        case Focus::Listeners:
            cursor_down_skip_headers(listener_rows_, listener_cursor_);
            break;
        case Focus::Connections:
            connection_cursor_down();
            break;
        case Focus::EntityDetail:
            break;  // handled in handle_key_entity_detail
    }
}

void ControllerTuiApp::focus_left()
{
    switch (focus_) {
        case Focus::Talkers:
            break;
        case Focus::Listeners:
            focus_ = Focus::Talkers;
            set_status("Talker sources");
            break;
        case Focus::Connections:
            focus_ = Focus::Listeners;
            set_status("Listener sinks");
            break;
        case Focus::EntityDetail:
            break;
    }
}

void ControllerTuiApp::try_focus_connections()
{
    if (connections_.empty()) {
        set_status("No active connections");
        return;
    }
    focus_ = Focus::Connections;
    if (connection_cursor_ >= connections_.size()) {
        connection_cursor_ = 0;
    }
    set_status("Connections — [D] to disconnect selected");
}

void ControllerTuiApp::focus_right()
{
    switch (focus_) {
        case Focus::Talkers:
            focus_ = Focus::Listeners;
            set_status("Listener sinks");
            break;
        case Focus::Listeners:
            try_focus_connections();
            break;
        case Focus::Connections:
            break;
        case Focus::EntityDetail:
            break;
    }
}

void ControllerTuiApp::begin_connect()
{
    if (focus_ != Focus::Talkers) {
        set_status("Move to Talker pane (h/Left) to start a connect");
        return;
    }
    if (talker_rows_.empty() || talker_cursor_ >= talker_rows_.size()) {
        set_status("No talker sources discovered");
        return;
    }
    auto const& row = talker_rows_[talker_cursor_];
    if (row.kind != StreamRowKind::Talker) {
        set_status("Move cursor to a talker source row (T)");
        return;
    }
    pending_ = StreamRequest{};
    pending_->talker_entity_id = row.entity_id;
    pending_->talker_unique_id = row.stream_index;
    mode_ = ControllerMode::SelectListener;
    focus_ = Focus::Listeners;
    set_status(
        std::format("Talker {}:{}  — select LISTENER sink and press Enter (Esc to cancel)", row.entity_name, row.stream_index));
}

void ControllerTuiApp::begin_disconnect()
{
    // In the Connections pane, D disconnects the selected connection directly
    if (focus_ == Focus::Connections) {
        disconnect_selected_connection();
        return;
    }

    if (focus_ != Focus::Talkers) {
        set_status("Move to Talker pane (h/Left) to start a disconnect");
        return;
    }
    if (talker_rows_.empty() || talker_cursor_ >= talker_rows_.size()) {
        set_status("No talker sources discovered");
        return;
    }
    auto const& row = talker_rows_[talker_cursor_];
    if (row.kind != StreamRowKind::Talker) {
        set_status("Move cursor to a talker source row (T)");
        return;
    }
    pending_ = StreamRequest{};
    pending_->talker_entity_id = row.entity_id;
    pending_->talker_unique_id = row.stream_index;
    mode_ = ControllerMode::DisconnectSelectListener;
    focus_ = Focus::Listeners;
    set_status(
        std::format(
            "Talker {}:{}  — select LISTENER sink to disconnect (Enter, Esc to cancel)", row.entity_name, row.stream_index));
}

void ControllerTuiApp::confirm_selection()
{
    if (listener_cursor_ >= listener_rows_.size() || !pending_.has_value()) {
        return;
    }
    auto const& row = listener_rows_[listener_cursor_];
    if (row.kind != StreamRowKind::Listener) {
        set_status("Move cursor to a listener sink row (L)");
        return;
    }
    pending_->listener_entity_id = row.entity_id;
    pending_->listener_unique_id = row.stream_index;

    if (mode_ == ControllerMode::SelectListener) {
        pending_actions_.push_back(ControllerAction{.kind = ControllerActionKind::ConnectStream, .request = *pending_});
        set_status("Connecting...");
    } else {
        pending_actions_.push_back(ControllerAction{.kind = ControllerActionKind::DisconnectStream, .request = *pending_});
        set_status("Disconnecting...");
    }

    mode_ = ControllerMode::Normal;
    pending_ = std::nullopt;
    focus_ = Focus::Talkers;
}

void ControllerTuiApp::cancel_selection()
{
    mode_ = ControllerMode::Normal;
    pending_ = std::nullopt;
    focus_ = Focus::Talkers;
    set_status("Cancelled");
}

void ControllerTuiApp::disconnect_selected_connection()
{
    if (connections_.empty() || connection_cursor_ >= connections_.size()) {
        set_status("No connection selected");
        return;
    }
    auto const& c = connections_[connection_cursor_];
    StreamRequest req{};
    req.talker_entity_id = c.talker_entity_id;
    req.talker_unique_id = c.talker_unique_id;
    req.listener_entity_id = c.listener_entity_id;
    req.listener_unique_id = c.listener_unique_id;
    pending_actions_.push_back(ControllerAction{.kind = ControllerActionKind::DisconnectStream, .request = req});
    set_status("Disconnecting...");
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

namespace {

auto pad_or_truncate(std::string s, int width) -> std::string
{
    if (static_cast<int>(s.size()) < width) {
        s.append(static_cast<size_t>(width) - s.size(), ' ');
    } else if (static_cast<int>(s.size()) > width) {
        s.resize(static_cast<size_t>(width));
    }
    return s;
}

auto format_header_row(StreamRow const& row, int width) -> std::string
{
    // Header: " EntityName" — entity name takes the full width
    std::string line = " ";
    line += row.entity_name;
    return pad_or_truncate(std::move(line), width);
}

auto format_stream_row(StreamRow const& row, bool is_cursor, bool is_pending, int width) -> std::string
{
    char kind_char = (row.kind == StreamRowKind::Talker) ? 'T' : 'L';
    // Layout: "  > * Tnn  stream_name  format"
    // Indented under its header. Prefix "  > * Tnn  " = 11 chars.
    int const prefix_len = 11;
    int const remaining = width - prefix_len;
    if (remaining <= 0) {
        return std::string(static_cast<size_t>(width), ' ');
    }

    std::string fmt = row.format.empty() ? std::string{"(format pending...)"} : row.format;
    std::string sname = row.stream_name;

    // Split remaining space: ~50% for stream name, ~50% for format
    if (!sname.empty()) {
        size_t const name_cap = static_cast<size_t>(remaining) / 2;
        if (sname.size() > name_cap) {
            sname.resize(name_cap);
        }
        size_t const fmt_cap = static_cast<size_t>(remaining) - sname.size() - 2;  // -2 for separator
        if (fmt.size() > fmt_cap) {
            fmt.resize(fmt_cap);
        }
    } else if (fmt.size() > static_cast<size_t>(remaining)) {
        fmt.resize(static_cast<size_t>(remaining));
    }

    std::string line;
    if (!sname.empty()) {
        line = std::format(
            "  {}{} {}{}  {}  {}", is_cursor ? ">" : " ", is_pending ? "*" : " ", kind_char, row.stream_index, sname, fmt);
    } else {
        line = std::format("  {}{} {}{}  {}", is_cursor ? ">" : " ", is_pending ? "*" : " ", kind_char, row.stream_index, fmt);
    }
    return pad_or_truncate(std::move(line), width);
}

auto format_connection_line(ActiveConnection const& c, bool is_cursor, int width) -> std::string
{
    std::string const t_name = c.talker_name.empty() ? ieee::to_string(c.talker_entity_id) : c.talker_name;
    std::string const l_name = c.listener_name.empty() ? ieee::to_string(c.listener_entity_id) : c.listener_name;
    std::string line = std::format(
        "{} {}:{}->{}:{}",
        is_cursor ? ">" : " ",
        t_name.substr(0, 10),
        c.talker_unique_id,
        l_name.substr(0, 10),
        c.listener_unique_id);
    if (!c.talker_format.empty()) {
        line += "  ";
        line += c.talker_format;
    }
    if (static_cast<int>(line.size()) < width) {
        line.append(static_cast<size_t>(width) - line.size(), ' ');
    } else if (static_cast<int>(line.size()) > width) {
        line.resize(static_cast<size_t>(width));
    }
    return line;
}

}  // namespace

void ControllerTuiApp::render_header(tui::TerminalTui& tui, int talker_width, int listener_width, int conn_width) const
{
    int const total_cols = tui.cols();

    // ---- Row 1: title + info ----
    tui.move_cursor(1, 1);
    tui.set_bold(true);
    tui.write("ATDECC Controller");
    std::string const right_info = std::format(
        "[{}] T:{} L:{} C:{}",
        interface_name_.empty() ? "?" : interface_name_,
        talker_rows_.size(),
        listener_rows_.size(),
        connections_.size());
    int const right_col = total_cols - static_cast<int>(right_info.size()) + 1;
    if (right_col > 20) {
        tui.write_at(1, right_col, right_info);
    }
    tui.reset_style();

    tui.write_rule(2);

    // ---- Row 3: column headers ----
    tui.set_bold(true);
    tui.write_at(3, 1, std::string(" Talker Sources").substr(0, static_cast<size_t>(talker_width)));
    tui.write_at(3, talker_width + 1, std::string(" Listener Sinks").substr(0, static_cast<size_t>(listener_width)));
    tui.write_at(3, talker_width + listener_width + 1, std::string(" Connections").substr(0, static_cast<size_t>(conn_width)));
    tui.reset_style();
}

namespace {
auto compute_scroll(size_t cursor, int list_rows) -> size_t
{
    if (list_rows > 0 && cursor >= static_cast<size_t>(list_rows)) {
        return cursor - static_cast<size_t>(list_rows) + 1;
    }
    return 0;
}

void render_stream_row(tui::TerminalTui& tui, StreamRow const& r, bool is_cursor, bool is_pending, int width)
{
    if (r.kind == StreamRowKind::Header) {
        tui.set_bold(true);
        tui.write(format_header_row(r, width));
        tui.reset_style();
    } else {
        if (is_pending) {
            tui.set_reverse(true);
        }
        tui.write(format_stream_row(r, is_cursor, is_pending, width));
        if (is_pending) {
            tui.reset_style();
        }
    }
}
}  // namespace

auto ControllerTuiApp::is_pending_talker_row(StreamRow const& r) const noexcept -> bool
{
    if (!pending_.has_value() || r.kind != StreamRowKind::Talker) {
        return false;
    }
    return r.entity_id == pending_->talker_entity_id && r.stream_index == pending_->talker_unique_id;
}

void ControllerTuiApp::render_talker_cell(tui::TerminalTui& tui, size_t row_abs_idx, int width) const
{
    if (row_abs_idx >= talker_rows_.size()) {
        tui.write(std::string(static_cast<size_t>(width), ' '));
        return;
    }
    auto const& r = talker_rows_[row_abs_idx];
    bool const is_cursor = (focus_ == Focus::Talkers) && (row_abs_idx == talker_cursor_);
    render_stream_row(tui, r, is_cursor, is_pending_talker_row(r), width);
}

void ControllerTuiApp::render_listener_cell(tui::TerminalTui& tui, size_t row_abs_idx, int width) const
{
    if (row_abs_idx >= listener_rows_.size()) {
        tui.write(std::string(static_cast<size_t>(width), ' '));
        return;
    }
    auto const& r = listener_rows_[row_abs_idx];
    bool const is_cursor = (focus_ == Focus::Listeners) && (row_abs_idx == listener_cursor_);
    render_stream_row(tui, r, is_cursor, false, width);
}

void ControllerTuiApp::render_connection_cell(tui::TerminalTui& tui, size_t row_abs_idx, int width) const
{
    if (row_abs_idx >= connections_.size()) {
        tui.write(std::string(static_cast<size_t>(width), ' '));
        return;
    }
    bool const is_cursor = (focus_ == Focus::Connections) && (row_abs_idx == connection_cursor_);
    tui.write(format_connection_line(connections_[row_abs_idx], is_cursor, width));
}

void ControllerTuiApp::render_lists(tui::TerminalTui& tui, int talker_width, int listener_width, int conn_width) const
{
    int const total_rows = tui.rows();
    int const header_rows = 3;
    int const footer_rows = 4;
    int const list_rows = total_rows - header_rows - footer_rows;

    size_t const talker_scroll = compute_scroll(talker_cursor_, list_rows);
    size_t const listener_scroll = compute_scroll(listener_cursor_, list_rows);
    size_t const conn_scroll = compute_scroll(connection_cursor_, list_rows);

    for (int row_idx = 0; row_idx < list_rows; ++row_idx) {
        int const screen_row = header_rows + row_idx + 1;
        auto const offset = static_cast<size_t>(row_idx);

        tui.move_cursor(screen_row, 1);
        render_talker_cell(tui, talker_scroll + offset, talker_width);

        tui.move_cursor(screen_row, talker_width + 1);
        render_listener_cell(tui, listener_scroll + offset, listener_width);

        tui.move_cursor(screen_row, talker_width + listener_width + 1);
        render_connection_cell(tui, conn_scroll + offset, conn_width);
    }
}

auto ControllerTuiApp::build_normal_action_bar() const -> std::string
{
    switch (focus_) {
        case Focus::Talkers:
            return "[Enter] details  [C]onnect  [D]isconnect  [i]dentify  s/S start/stop  [R]efresh  [Q]uit  j/k  l->";
        case Focus::Listeners:
            return "[Enter] details  [i]dentify  s/S start/stop  j/k  h<->l  [R]efresh  [Q]uit";
        case Focus::Connections:
            return "[D]isconnect selected  j/k  h/Left->Listeners  [R]efresh  [Q]uit";
        case Focus::EntityDetail:
            return "j/k scroll  [i]dentify  [R]efresh  [Esc] back";
    }
    return {};
}

auto ControllerTuiApp::build_action_bar() const -> std::string
{
    switch (mode_) {
        case ControllerMode::Normal:
            return build_normal_action_bar();
        case ControllerMode::SelectListener:
            return "Select LISTENER sink: j/k move  [Enter] connect  [Esc] cancel";
        case ControllerMode::DisconnectSelectListener:
            return "Select LISTENER sink: j/k move  [Enter] disconnect  [Esc] cancel";
        case ControllerMode::EntityDetail:
            return "j/k scroll  [i]dentify  [R]efresh  [Esc] back";
    }
    return {};
}

void ControllerTuiApp::render_footer(tui::TerminalTui& tui) const
{
    int const total_rows = tui.rows();
    int const total_cols = tui.cols();
    int const rule1_row = total_rows - 3;
    int const status_row = total_rows - 2;
    int const rule2_row = total_rows - 1;
    int const action_row = total_rows;

    tui.write_rule(rule1_row);

    {
        std::string status_line = std::format("Status: {}", status_);
        if (static_cast<int>(status_line.size()) < total_cols) {
            status_line.append(static_cast<size_t>(total_cols) - status_line.size(), ' ');
        }
        tui.write_at(status_row, 1, status_line);
    }

    tui.write_rule(rule2_row);

    tui.move_cursor(action_row, 1);
    std::string const action_bar = build_action_bar();
    tui.write(action_bar);
    if (static_cast<int>(action_bar.size()) < total_cols) {
        tui.write(std::string(static_cast<size_t>(total_cols) - action_bar.size(), ' '));
    }
}

void ControllerTuiApp::render_normal_layout(tui::TerminalTui& tui) const
{
    int const total_cols = tui.cols();
    int const talker_width = total_cols / 3;
    int const listener_width = total_cols / 3;
    int const conn_width = total_cols - talker_width - listener_width;

    render_header(tui, talker_width, listener_width, conn_width);
    render_lists(tui, talker_width, listener_width, conn_width);
    render_footer(tui);
}

void ControllerTuiApp::render_entity_detail_layout(tui::TerminalTui& tui) const
{
    int const total_cols = tui.cols();
    int const total_rows = tui.rows();
    int const header_rows = 3;
    int const footer_rows = 4;
    int const list_rows = total_rows - header_rows - footer_rows;
    int const list_w = total_cols / 3;
    int const detail_w = total_cols - list_w;

    // Header
    tui.move_cursor(1, 1);
    tui.set_bold(true);
    tui.write("ATDECC Controller");
    std::string const detail_name = entity_detail_ ? entity_detail_->name : "...";
    std::string const right_info = std::format("Detail: {}", detail_name);
    int const right_col = total_cols - static_cast<int>(right_info.size()) + 1;
    if (right_col > 20) {
        tui.write_at(1, right_col, right_info);
    }
    tui.reset_style();
    tui.write_rule(2);

    // Column headers
    tui.set_bold(true);
    tui.write_at(3, 1, std::string(" Entities").substr(0, static_cast<size_t>(list_w)));
    tui.write_at(3, list_w + 1, std::string(" Descriptor Detail").substr(0, static_cast<size_t>(detail_w)));
    tui.reset_style();

    // Left pane: entity list
    size_t const talker_scroll = compute_scroll(talker_cursor_, list_rows);
    for (int row_idx = 0; row_idx < list_rows; ++row_idx) {
        int const screen_row = header_rows + row_idx + 1;
        tui.move_cursor(screen_row, 1);
        render_talker_cell(tui, talker_scroll + static_cast<size_t>(row_idx), list_w);
    }

    // Right pane: scrollable detail lines
    for (int row_idx = 0; row_idx < list_rows; ++row_idx) {
        int const screen_row = header_rows + row_idx + 1;
        tui.move_cursor(screen_row, list_w + 1);

        if (entity_detail_) {
            size_t const line_idx = detail_scroll_ + static_cast<size_t>(row_idx);
            if (line_idx < entity_detail_->lines.size()) {
                auto const& dl = entity_detail_->lines[line_idx];
                if (dl.bold) {
                    tui.set_bold(true);
                }
                std::string text = dl.text;
                tui.write(pad_or_truncate(std::move(text), detail_w));
                if (dl.bold) {
                    tui.reset_style();
                }
            } else {
                tui.write(std::string(static_cast<size_t>(detail_w), ' '));
            }
        } else {
            if (row_idx == 0) {
                tui.write(pad_or_truncate("  Loading...", detail_w));
            } else {
                tui.write(std::string(static_cast<size_t>(detail_w), ' '));
            }
        }
    }

    render_footer(tui);
}

void ControllerTuiApp::render(tui::TerminalTui& tui) const
{
    if (mode_ == ControllerMode::EntityDetail) {
        render_entity_detail_layout(tui);
    } else {
        render_normal_layout(tui);
    }
    tui.flush();
}

}  // namespace statusbar::atdecc_tools
