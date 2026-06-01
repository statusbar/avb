#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Session Traversal Utilities for NAT (RFC 8489) plus a private REGISTER
/// extension that performs reflexive-address discovery and peer
/// rendezvous in a single message.
///
/// The codec primitives (header / attributes / XOR-MAPPED-ADDRESS) are
/// standards-clean and could be used to implement a plain Binding client
/// against a public STUN server.

#include "statusbar/stun/stun_auth.hpp"
#include "statusbar/stun/stun_client.hpp"
#include "statusbar/stun/stun_client_sm.hpp"
#include "statusbar/stun/stun_constants.hpp"
#include "statusbar/stun/stun_error.hpp"
#include "statusbar/stun/stun_message.hpp"
#include "statusbar/stun/stun_register.hpp"
#include "statusbar/stun/stun_rendezvous.hpp"
#include "statusbar/stun/stun_server.hpp"
#include "statusbar/stun/stun_server_sm.hpp"
#include "statusbar/stun/stun_types.hpp"

namespace statusbar::stun {}
