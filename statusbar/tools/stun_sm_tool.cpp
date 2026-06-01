// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// STUN State Machine Documentation Tool
// Generates DOT (Graphviz) or Markdown documentation for STUN client + server
// session state machines (RFC 8489 + private REGISTER extension).

#include "statusbar/sm/sm.hpp"
#include "statusbar/stun/stun_client_sm.hpp"
#include "statusbar/stun/stun_server_sm.hpp"

#include <array>

using statusbar::sm::make_sm_info;

constexpr auto state_machines = std::array{
    make_sm_info<statusbar::stun::ClientStateMachine>(
        "stun_client_sm", "STUN Client Session", "RFC 8489 (STUN) + private REGISTER extension for rendezvous"),
    make_sm_info<statusbar::stun::ServerSessionStateMachine>(
        "stun_server_session_sm", "STUN Server Session", "RFC 8489 (STUN) + private REGISTER extension for rendezvous"),
};

int main(int argc, char* argv[])
{
    return statusbar::sm::sm_tool(argc, argv, state_machines);
}
