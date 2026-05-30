#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// ACMP Controller State Machine - IEEE 1722.1-2021 Clause 8.2.3
/// Implements the ATDECC Controller state machine for connection management

#include "statusbar/atdecc/atdecc_acmp.hpp"
#include "statusbar/container/container_slot_table.hpp"
#include "statusbar/ieee/ieee.hpp"
#include "statusbar/sg14/inplace_function.h"
#include "statusbar/sm/sm.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <source_location>

namespace statusbar::atdecc {

using statusbar::container::SlotTable;

//
// Controller State Machine Definition (Figure 8-2)
//

/// Controller state machine states
enum class ControllerState : uint8_t
{
    Start,     // Initial state after construction
    Waiting,   // Idle state, waiting for commands or responses
    Command,   // Sending a command
    Timeout,   // Handling a timeout
    Response,  // Processing a received response
    Count
};

/// Controller state machine events
enum class ControllerEvent : uint8_t
{
    UCT,           // Unconditional transition
    DoCommand,     // User requested to send a command
    DoTerminate,   // Shutdown requested
    RcvdResponse,  // Received a matching response
    RcvdOther,     // Received non-matching response (ignored)
    Timeout,       // An inflight command timed out
    Count
};

/// Controller state machine context - holds all state and callbacks
struct ControllerContext
{
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    /// Compile-time hard capacity for inflight commands. Tests may still
    /// construct a smaller runtime soft cap via the constructor argument.
    static constexpr size_t MAX_INFLIGHT = 16;

    /// Default-construct with the full compile-time MAX_INFLIGHT capacity.
    ControllerContext() noexcept = default;

    /// Construct with a runtime soft cap. Intended for tests that want to
    /// deliberately exercise a "table full" condition at a smaller bound
    /// than the compile-time maximum. Values greater than MAX_INFLIGHT are
    /// clamped.
    /// @param max_inflight Runtime cap on simultaneous inflight commands.
    explicit ControllerContext(size_t max_inflight) noexcept
        : inflight_{max_inflight}
    {}

    // Entity ID of this controller
    ieee::Eui64 my_id{};

    // Received command/response being processed
    AcmpCommandResponse rcvd_cmd_resp{};

    // Parameters for the next command to send
    AcmpCommandParams command_params{};

    // Sequence ID counter
    uint16_t next_sequence_id{0};

    // Index of the inflight command currently being processed (for timeout/response)
    size_t current_inflight_index{0};

    // Callbacks - must be set before use
    statusbar::sg14::inplace_function<bool(AcmpCommandResponse const&), 64> tx_command{};
    statusbar::sg14::inplace_function<void(AcmpCommandResponse const&), 64> process_response{};

    //
    // Inflight command management — thin wrappers over SlotTable so
    // existing call sites keep compiling unchanged.
    //

    /// Get current inflight count.
    [[nodiscard]] auto inflight_count() const noexcept -> size_t { return inflight_.size(); }

    /// Get the runtime soft-cap capacity (may be less than MAX_INFLIGHT for tests).
    [[nodiscard]] auto max_inflight() const noexcept -> size_t { return inflight_.capacity(); }

    /// Find an inflight command matching the response.
    /// Returns index if found, `MAX_INFLIGHT` (equal to the compile-time
    /// ceiling) if not — `index >= max_inflight()` remains a valid
    /// "not found" check regardless of the runtime soft cap.
    [[nodiscard]] auto find_inflight(AcmpCommandResponse const& resp) const noexcept -> size_t
    {
        return inflight_.find_if([&resp](InflightCommand const& e) {
            return e.command.controller_entity_id == resp.controller_entity_id && e.command.sequence_id == resp.sequence_id &&
                (e.command.message_type() + 1) == resp.message_type();
        });
    }

    /// Find an inflight command that has timed out.
    /// Returns index if found, `MAX_INFLIGHT` if not.
    [[nodiscard]] auto find_timed_out(TimePoint const current_time) const noexcept -> size_t
    {
        return inflight_.find_if([current_time](InflightCommand const& e) { return current_time >= e.timeout_time; });
    }

    /// Add a new inflight command. Returns true if added, false if at capacity.
    auto add_inflight(AcmpCommandResponse const& cmd, TimePoint const timeout_time) -> bool
    {
        return inflight_.add(
            InflightCommand{
                .timeout_time = timeout_time,
                .retried = false,
                .command = cmd,
                .original_sequence_id = cmd.sequence_id,
                .valid = true,
            });
    }

    /// Remove an inflight command by index. No-op if out of range.
    void remove_inflight(size_t const index) noexcept { inflight_.remove(index); }

    /// Get inflight command at index (for timeout/response handling).
    /// Returns nullptr if the index is out of range.
    [[nodiscard]] auto get_inflight(size_t const index) noexcept -> InflightCommand* { return inflight_.get(index); }

    [[nodiscard]] auto get_inflight(size_t const index) const noexcept -> InflightCommand const* { return inflight_.get(index); }

  private:
    SlotTable<InflightCommand, MAX_INFLIGHT> inflight_{};
};

//
// Controller State Machine Actions
//

namespace controller_actions {

/// Action: Send a command (COMMAND state entry)
/// @param ctx Controller state machine context
/// @param event_time Time point when the event occurred
void send_command(ControllerContext& ctx, sm::TimePoint event_time);

/// Action: Handle timeout (TIMEOUT state entry)
/// @param ctx Controller state machine context
/// @param event_time Time point when the timeout occurred
void handle_timeout(ControllerContext& ctx, sm::TimePoint event_time);

/// Action: Process a received response (RESPONSE state entry)
/// @param ctx Controller state machine context
void handle_response(ControllerContext& ctx, sm::TimePoint event_time);

}  // namespace controller_actions

//
// Controller State Machine Definition Type
//

struct ControllerDef
{
    using Context = ControllerContext;
    using State = ControllerState;
    using Event = ControllerEvent;
};

//
// Controller Transition Table
//

/// Build the controller state machine transition table
consteval auto make_controller_table()
{
    using T = sm::Transitions<ControllerDef>;
    using State = ControllerState;
    using Event = ControllerEvent;

    sm::TransitionTable<ControllerDef> t{};

    // From START state - UCT to Waiting
    t.at(State::Start, Event::UCT) = T::transition(State::Waiting);

    // From WAITING state
    t.at(State::Waiting, Event::DoCommand) = T::transition(State::Command);
    t.at(State::Waiting, Event::RcvdResponse) = T::transition(State::Response);
    t.at(State::Waiting, Event::Timeout) = T::transition(State::Timeout);
    // DoTerminate and RcvdOther stay in Waiting (no transition defined = ignored)

    // From COMMAND state - UCT back to WAITING after sending
    t.at(State::Command, Event::UCT) = T::action<controller_actions::send_command>(State::Waiting);

    // From TIMEOUT state - UCT back to WAITING after handling
    t.at(State::Timeout, Event::UCT) = T::action<controller_actions::handle_timeout>(State::Waiting);

    // From RESPONSE state - UCT back to WAITING after processing
    t.at(State::Response, Event::UCT) = T::action<controller_actions::handle_response>(State::Waiting);

    return t;
}

/// Static controller transition table
inline constexpr auto controller_table = make_controller_table();

//
// Controller State Machine Type
//

/// ACMP Controller state machine
template <typename Observer = sm::NullObserver>
using AcmpControllerStateMachine = sm::StateMachine<ControllerDef, controller_table, Observer>;

//
// Controller State Machine Helper Functions
//

/// Check if a received response matches an inflight command and should trigger RcvdResponse event
/// @param ctx Controller state machine context
/// @param resp Received ACMP response to check
[[nodiscard]] auto controller_should_handle_response(ControllerContext const& ctx, AcmpCommandResponse const& resp) noexcept
    -> bool;

/// Check if any inflight command has timed out
/// @param ctx Controller state machine context
/// @param current_time Current time point to compare against timeout deadlines
[[nodiscard]] inline auto controller_has_timeout(ControllerContext const& ctx, sm::TimePoint const current_time) noexcept -> bool
{
    return ctx.find_timed_out(current_time) < ctx.max_inflight();
}

}  // namespace statusbar::atdecc
