// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// SRP / MRP State Machine Documentation Tool
// Generates DOT (Graphviz) or Markdown documentation for the IEEE 802.1Q
// Multiple Registration Protocol (MRP) base state machines.

#include "statusbar/sm/sm.hpp"
#include "statusbar/srp/srp_mrp_applicant_sm.hpp"
#include "statusbar/srp/srp_mrp_leaveall_sm.hpp"
#include "statusbar/srp/srp_mrp_periodic_sm.hpp"
#include "statusbar/srp/srp_mrp_registrar_sm.hpp"

#include <array>

using statusbar::sm::make_sm_info;

constexpr auto state_machines = std::array{
    make_sm_info<statusbar::srp::mrp::applicant_sm::Machine>("applicant_sm", "MRP Applicant", "IEEE 802.1Q-2014 Clause 10.7.7"),
    make_sm_info<statusbar::srp::mrp::registrar_sm::Machine>(
        "registrar_sm", "MRP Registrar", "IEEE 802.1Q-2014 Clause 10.7.8 Table 10-4"),
    make_sm_info<statusbar::srp::mrp::leaveall_sm::Machine>("leaveall_sm", "MRP LeaveAll", "IEEE 802.1Q-2014 Clause 10.7.5.22"),
    make_sm_info<statusbar::srp::mrp::periodic_sm::Machine>("periodic_sm", "MRP Periodic", "IEEE 802.1Q-2014 Clause 10.7.5.23"),
};

int main(int argc, char* argv[])
{
    return statusbar::sm::sm_tool(argc, argv, state_machines);
}
