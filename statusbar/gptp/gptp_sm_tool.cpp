// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// gPTP State Machine Documentation Tool
// Generates DOT (Graphviz) or Markdown documentation for gPTP state machines.

#include "statusbar/gptp/gptp_port_state_sm.hpp"
#include "statusbar/sm/sm.hpp"

#include <array>

using statusbar::sm::make_sm_info;

constexpr auto state_machines = std::array{
    make_sm_info<statusbar::gptp::port_state_sm::Machine>(
        "port_state_sm",
        "gPTP Slave-Role Port FSM",
        "IEEE 802.1AS-2020 Clause 10 (slave-only lifecycle abstraction over PortSyncSyncReceive / MDPdelayReq / SiteSyncSync)"),
};

int main(int argc, char* argv[])
{
    return statusbar::sm::sm_tool(argc, argv, state_machines);
}
