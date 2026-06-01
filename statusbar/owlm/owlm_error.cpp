// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/owlm/owlm_error.hpp"

namespace statusbar::owlm {

auto OwlmErrorCategory::message(int ev) const -> std::string
{
    switch (static_cast<OwlmError>(ev)) {
        case OwlmError::InvalidMagic:
            return "Invalid OWLM magic bytes";
        case OwlmError::UnsupportedVersion:
            return "Unsupported OWLM version";
        case OwlmError::ReservedFlagsSet:
            return "Reserved flags bits are set";
        case OwlmError::DatagramTooShort:
            return "Datagram smaller than OWLM header";
        case OwlmError::InvalidInterval:
            return "Sender's tx_interval_us is out of range";
        case OwlmError::SenderNotSynced:
            return "Sender's tx_gptp_ns is not positive";
        default:
            return "Unknown OWLM error";
    }
}

auto owlm_error_category() noexcept -> std::error_category const&
{
    static OwlmErrorCategory const instance;
    return instance;
}

auto make_error_code(OwlmError e) noexcept -> std::error_code
{
    return {static_cast<int>(e), owlm_error_category()};
}

}  // namespace statusbar::owlm
