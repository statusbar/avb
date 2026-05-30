// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/gptp/gptp_messages.hpp"

namespace statusbar::gptp {

auto parse_gptp(std::span<uint8_t const> payload) -> std::optional<GptpMessage>
{
    if (payload.size() < MessageHeader::LENGTH) {
        return std::nullopt;
    }

    MessageHeader hdr;
    span_load(hdr, payload);

    switch (hdr.message_type()) {
        case MESSAGE_TYPE_SYNC:
            if (payload.size() >= SyncMessage::LENGTH) {
                SyncMessage msg;
                span_load(msg, payload);
                return msg;
            }
            return GptpTruncated{hdr};

        case MESSAGE_TYPE_FOLLOW_UP:
            if (payload.size() >= FollowUpMessage::LENGTH) {
                FollowUpMessage msg;
                span_load(msg, payload);
                return msg;
            }
            return GptpTruncated{hdr};

        case MESSAGE_TYPE_PDELAY_REQ:
            if (payload.size() >= PdelayReqMessage::LENGTH) {
                PdelayReqMessage msg;
                span_load(msg, payload);
                return msg;
            }
            return GptpTruncated{hdr};

        case MESSAGE_TYPE_PDELAY_RESP:
            if (payload.size() >= PdelayRespMessage::LENGTH) {
                PdelayRespMessage msg;
                span_load(msg, payload);
                return msg;
            }
            return GptpTruncated{hdr};

        case MESSAGE_TYPE_PDELAY_RESP_FOLLOW_UP:
            if (payload.size() >= PdelayRespFollowUpMessage::LENGTH) {
                PdelayRespFollowUpMessage msg;
                span_load(msg, payload);
                return msg;
            }
            return GptpTruncated{hdr};

        case MESSAGE_TYPE_ANNOUNCE:
            if (payload.size() >= AnnounceMessage::LENGTH) {
                AnnounceMessage msg;
                span_load(msg, payload);
                return msg;
            }
            return GptpTruncated{hdr};

        case MESSAGE_TYPE_SIGNALING:
            // Signaling messages are variable-length; we parse the
            // fixed 44-byte portion (header + targetPortIdentity) and
            // leave the TLV suffix for the port-level handler to parse
            // out of the remaining bytes.
            if (payload.size() >= SignalingMessage::FIXED_LENGTH) {
                SignalingMessage msg;
                span_load(msg, payload);
                return msg;
            }
            return GptpTruncated{hdr};

        default:
            return hdr;
    }
}

}  // namespace statusbar::gptp
