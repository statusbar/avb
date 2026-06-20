#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// Per-node identity defaults for AVB entities. Centralizes the "make every
/// node on the wire unique" logic the entity tools otherwise hand-roll (or
/// forget — a hardcoded entity_id makes two nodes running the same descriptor
/// blob collide).

#include "statusbar/ieee/ieee_ethernet.hpp"
#include "statusbar/net/net_posix_util.hpp"

#include <unistd.h>

#include <array>
#include <print>
#include <string>
#include <string_view>

namespace statusbar::avb_entity {

/// Fill per-node identity defaults in place:
///  - `entity_id` (when unset): the `interface_name` MAC as a modified EUI-64
///    so every node is unique; falls back to `fallback_id` if the MAC can't be
///    read (with a warning).
///  - `entity_name` (when empty): the host's name, so a controller can address
///    the node by name; falls back to `fallback_name`.
///
/// Idempotent: an already-set id / non-empty name (e.g. from `--entity.id` /
/// `--entity.name`) is left untouched.
inline void apply_node_identity_defaults(
    std::string_view interface_name,
    ieee::Eui64& entity_id,
    std::string& entity_name,
    ieee::Eui64 const& fallback_id,
    std::string_view fallback_name)
{
    if (!entity_id.is_set()) {
        if (auto const mac = net::read_interface_mac(interface_name)) {
            entity_id = mac->to_modified_eui64();
        } else {
            std::print(stderr, "Warning: could not read MAC of {}; using fallback entity ID\n", interface_name);
            entity_id = fallback_id;
        }
    }
    if (entity_name.empty()) {
        std::array<char, 256> host{};
        entity_name = (::gethostname(host.data(), host.size() - 1) == 0) ? std::string{host.data()} : std::string{fallback_name};
    }
}

}  // namespace statusbar::avb_entity
