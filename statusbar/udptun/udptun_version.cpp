// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_version.hpp"

namespace statusbar::udptun {

// Translation-unit anchor — keeps the module library non-empty so the
// build system has a real archive to link against. Real content lands
// as the framework primitives migrate from owlm.
[[nodiscard]] auto udptun_version() noexcept -> std::string_view
{
    return udptun_version_string;
}

}  // namespace statusbar::udptun
