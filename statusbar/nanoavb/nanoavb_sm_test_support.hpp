#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Test support for nanoavb state-machine tests.
//
// Provides a transition observer that records the name of the most recently
// executed action, plus a small fixture that bundles a StateMachine with that
// observer. This replaces the former per-Context `last_action` probe: tests
// read `machine.last_action` instead of `ctx.last_action`, and the action
// name now comes from the framework observer rather than a hand-maintained
// Context field.

#include "statusbar/sm/sm_core.hpp"

#include <string_view>
#include <utility>

namespace statusbar::nanoavb::sm_test {

using statusbar::sm::StateMachine;

/// Observer that captures the name of the most recently executed action.
///
/// Only action-bearing transitions update it (no-action transitions carry an
/// empty action name and are ignored), which matches the semantics of the old
/// `ctx.last_action` field: it reflects the last action that actually ran.
/// Action-bearing self-transitions are included (the framework notifies the
/// observer whenever an action runs, even when the state does not change).
template <typename Def>
struct ActionRecorder
{
    using State = typename Def::State;
    using Event = typename Def::Event;

    std::string_view* last_action{nullptr};

    void operator()(State /*old_state*/, Event /*event*/, std::string_view action_name, State /*new_state*/) const
    {
        if (last_action != nullptr && !action_name.empty()) {
            *last_action = action_name;
        }
    }
};

/// A StateMachine bundled with an ActionRecorder and the storage it writes to.
///
/// Tests read `.last_action` in place of the old `ctx.last_action`, and may
/// reset it (e.g. `machine.last_action = {}`) before exercising events that
/// should run no action. `handle_event`/`current_state` forward to the
/// underlying machine.
template <typename Def, auto const& Table>
struct Observed
{
    using State = typename Def::State;
    using Event = typename Def::Event;

    std::string_view last_action{};
    StateMachine<Def, Table, ActionRecorder<Def>> machine{ActionRecorder<Def>{&last_action}};

    Observed() = default;
    Observed(Observed const&) = delete;
    auto operator=(Observed const&) -> Observed& = delete;

    template <typename... Args>
    void handle_event(Args&&... args)
    {
        machine.handle_event(std::forward<Args>(args)...);
    }

    [[nodiscard]] auto current_state() const noexcept -> State { return machine.current_state(); }

    void reset() noexcept { machine.reset(); }
};

}  // namespace statusbar::nanoavb::sm_test
