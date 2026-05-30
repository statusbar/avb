// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/avtp/avtp_types.hpp"

namespace statusbar::avtp {

auto avtp_header_type_name(AvtpHeaderType const type) noexcept -> char const*
{
    switch (type) {
        case AvtpHeaderType::stream:
            return "Stream";
        case AvtpHeaderType::control:
            return "Control";
        case AvtpHeaderType::alternative:
            return "Alternative";
        case AvtpHeaderType::reserved:
        default:
            return "Reserved";
    }
}

auto avtp_encapsulation_name(AvtpEncapsulation const encap) noexcept -> char const*
{
    switch (encap) {
        case AvtpEncapsulation::continuous:
            return "Continuous";
        case AvtpEncapsulation::discrete:
            return "Discrete";
        case AvtpEncapsulation::reserved:
        default:
            return "Reserved";
    }
}

auto avtp_subtype_name(uint8_t const subtype) noexcept -> char const*
{
    switch (subtype) {
        case AvtpSubtype::iec_61883_iidc:
            return "61883_IIDC";
        case AvtpSubtype::mma_stream:
            return "MMA_STREAM";
        case AvtpSubtype::aaf:
            return "AAF";
        case AvtpSubtype::cvf:
            return "CVF";
        case AvtpSubtype::crf:
            return "CRF";
        case AvtpSubtype::tscf:
            return "TSCF";
        case AvtpSubtype::svf:
            return "SVF";
        case AvtpSubtype::rvf:
            return "RVF";
        case AvtpSubtype::aef_continuous:
            return "AEF_CONTINUOUS";
        case AvtpSubtype::vsf_stream:
            return "VSF_STREAM";
        case AvtpSubtype::ef_stream:
            return "EF_STREAM";
        case AvtpSubtype::ntscf:
            return "NTSCF";
        case AvtpSubtype::escf:
            return "ESCF";
        case AvtpSubtype::eecf:
            return "EECF";
        case AvtpSubtype::aef_discrete:
            return "AEF_DISCRETE";
        case AvtpSubtype::adp:
            return "ADP";
        case AvtpSubtype::aecp:
            return "AECP";
        case AvtpSubtype::acmp:
            return "ACMP";
        case AvtpSubtype::maap:
            return "MAAP";
        case AvtpSubtype::ef_control:
            return "EF_CONTROL";
        default:
            return "Reserved";
    }
}

}  // namespace statusbar::avtp
