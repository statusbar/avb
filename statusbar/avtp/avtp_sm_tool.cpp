// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// AVTP State Machine Documentation Tool
// Generates DOT (Graphviz) or Markdown documentation for AVTP state machines.

#include "statusbar/avtp/avtp_maap_sm.hpp"
#include "statusbar/sm/sm.hpp"

#include <array>

using statusbar::sm::make_sm_info;

constexpr auto state_machines = std::array{
    make_sm_info<statusbar::avtp::maap_sm::Machine>(
        "maap_sm", "MAAP (Multicast Address Acquisition Protocol)", "IEEE 1722-2025 Annex B.3"),
};

int main(int argc, char* argv[])
{
    return statusbar::sm::sm_tool(argc, argv, state_machines);
}
