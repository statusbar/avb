#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

//
// MDSyncReceive — IEEE 802.1AS-2020 Clause 10.2.7.
//
// Pairs received Sync messages with their matching FollowUp by
// sequence id, extracts the FollowUp Information TLV, and produces
// a complete MDSyncReceive indication that the PortSyncSyncReceive
// layer (and the servo) consume.
//
// This is modeled as a plain class rather than a statusbar::sm FSM —
// it has only two substantive states (Idle, WaitingForFollowUp), and
// the pairing logic needs runtime input data that doesn't map well
// onto the framework's void(Context&,TimePoint) action signature.
// The state is still tracked explicitly via a member enum for
// observability and per-spec faithfulness.
//

#include "statusbar/gptp/gptp_base.hpp"
#include "statusbar/gptp/gptp_header.hpp"
#include "statusbar/gptp/gptp_messages.hpp"
#include "statusbar/gptp/gptp_tlv.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>

namespace statusbar::gptp {

/// Output of a successful Sync+FollowUp pairing. Contains everything
/// the PortSyncSyncReceive / servo pipeline needs to compute the
/// master-to-local offset.
struct MDSyncReceiveIndication
{
    /// Sequence id of the paired Sync+FollowUp.
    uint16_t sequence_id{0};

    /// Source port identity from the Sync header (matches the FollowUp).
    SourcePortIdentity source_port_identity{};

    /// preciseOriginTimestamp from the FollowUp body — this is the
    /// grandmaster's time at Sync egress.
    Timestamp precise_origin_timestamp{};

    /// Sum of the Sync and FollowUp correctionField values, converted
    /// from wire 16.16 fixed-point ns to integer ns. In the slave
    /// follower this is added to precise_origin_timestamp before the
    /// phase error computation.
    int64_t correction_field_ns{0};

    /// FollowUp Information TLV fields (all zero / default if no TLV).
    int32_t cumulative_scaled_rate_offset{0};  // 2^-41 scale, 0 = no change
    uint16_t gm_time_base_indicator{0};
    ScaledNs last_gm_phase_change{};
    int32_t scaled_last_gm_freq_change{0};
    bool has_follow_up_tlv{false};

    /// Local hardware timestamp of the Sync frame arrival, in local
    /// clock nanoseconds (already corrected for RX PHY delay by the
    /// caller).
    int64_t sync_rx_local_ns{0};

    /// log2 of the advertised sync interval in seconds (from Sync
    /// header).
    int8_t log_message_interval{0};
};

/// Copy FollowUp Information TLV fields into an MDSyncReceiveIndication.
inline void apply_follow_up_tlv(FollowUpInformationTLV const& tlv, MDSyncReceiveIndication& ind) noexcept
{
    ind.cumulative_scaled_rate_offset = tlv.get_cumulative_scaled_rate_offset();
    ind.gm_time_base_indicator = tlv.gm_time_base_indicator.get();
    ind.last_gm_phase_change = tlv.last_gm_phase_change;
    ind.scaled_last_gm_freq_change = tlv.get_scaled_last_gm_freq_change();
    ind.has_follow_up_tlv = true;
}

/// MDSyncReceive pairing / matching logic.
class MDSyncReceive
{
  public:
    /// Substantive state. See Clause 10.2.7.
    enum class State : uint8_t
    {
        Idle,                ///< waiting for the next Sync
        WaitingForFollowUp,  ///< got a Sync, waiting for matching FollowUp
        Discard,             ///< port disabled; drop everything
    };

    MDSyncReceive() = default;

    [[nodiscard]] auto state() const noexcept -> State { return state_; }

    /// Transition to Discard — called when the port goes down or is
    /// administratively disabled. Any in-flight Sync is dropped.
    void disable() noexcept
    {
        state_ = State::Discard;
        pending_valid_ = false;
    }

    /// Transition out of Discard. Called when the port becomes usable
    /// again.
    void enable() noexcept
    {
        state_ = State::Idle;
        pending_valid_ = false;
    }

    /// Process a received Sync message.
    ///
    /// @param sync                 The parsed Sync message.
    /// @param sync_rx_local_ns     Local hardware timestamp of Sync
    ///                             frame arrival (already PHY-adjusted).
    void on_sync(SyncMessage const& sync, int64_t sync_rx_local_ns) noexcept
    {
        if (state_ == State::Discard) {
            return;
        }
        // Per Clause 10.2.7, a new Sync while WaitingForFollowUp
        // discards the prior pending Sync (it was never paired).
        pending_.sequence_id = sync.header.sequence_id;
        pending_.source_port_identity = sync.header.source_port_identity;
        pending_.sync_rx_local_ns = sync_rx_local_ns;
        pending_.log_message_interval = static_cast<int8_t>(sync.header.log_message_interval);
        // correctionField from the Sync header (16.16 scaled ns) — we
        // accumulate it into the total correction applied to the
        // precise origin timestamp at FollowUp time.
        pending_.sync_correction_raw = sync.header.correction_field();
        pending_valid_ = true;
        state_ = State::WaitingForFollowUp;
    }

    /// Process a received FollowUp message plus the trailing TLV bytes
    /// (if any). Returns a valid indication iff the FollowUp's
    /// sequence id matches the pending Sync AND the source port
    /// identity matches.
    ///
    /// @param follow_up                 The parsed 44-byte FollowUp.
    /// @param trailing_bytes_after_44   Bytes after the FollowUp's
    ///                                  44-byte fixed portion. Caller
    ///                                  passes the raw wire remainder
    ///                                  for TLV parsing.
    [[nodiscard]] auto on_follow_up(FollowUpMessage const& follow_up, std::span<uint8_t const> trailing_bytes_after_44) noexcept
        -> std::optional<MDSyncReceiveIndication>
    {
        if (state_ != State::WaitingForFollowUp) {
            return std::nullopt;
        }
        if (!pending_valid_) {
            return std::nullopt;
        }
        if (follow_up.header.sequence_id != pending_.sequence_id) {
            // Mismatched FollowUp — drop the pair, stay in WaitingForFollowUp
            // since the caller may retry the real FollowUp later.
            return std::nullopt;
        }
        if (follow_up.header.source_port_identity != pending_.source_port_identity) {
            return std::nullopt;
        }

        MDSyncReceiveIndication ind{};
        ind.sequence_id = pending_.sequence_id;
        ind.source_port_identity = pending_.source_port_identity;
        ind.precise_origin_timestamp = follow_up.precise_origin_timestamp;
        ind.sync_rx_local_ns = pending_.sync_rx_local_ns;
        ind.log_message_interval = pending_.log_message_interval;

        // Total correctionField = Sync header correction + FollowUp
        // header correction. Both are 16.16 fixed-point ns on the wire.
        // Convert to integer ns by >> 16.
        int64_t const total_correction_raw = pending_.sync_correction_raw + follow_up.header.correction_field();
        ind.correction_field_ns = total_correction_raw >> 16;

        // Parse the FollowUp Information TLV if present.
        if (trailing_bytes_after_44.size() >= FollowUpInformationTLV::LENGTH) {
            FollowUpInformationTLV tlv{};
            (void)load_unchecked(trailing_bytes_after_44.first(FollowUpInformationTLV::LENGTH), &tlv);
            if (tlv.is_valid()) {
                apply_follow_up_tlv(tlv, ind);
            }
        }

        // Pair consumed; return to Idle awaiting the next Sync.
        pending_valid_ = false;
        state_ = State::Idle;
        return ind;
    }

    /// Called when the SyncReceiptTimeout fires (no FollowUp arrived
    /// within the expected window). Drops the pending Sync.
    void on_sync_receipt_timeout() noexcept
    {
        pending_valid_ = false;
        if (state_ != State::Discard) {
            state_ = State::Idle;
        }
    }

    /// True iff a Sync has been received and we are waiting for its
    /// matching FollowUp.
    [[nodiscard]] auto has_pending_sync() const noexcept -> bool { return pending_valid_; }

    /// Sequence id of the pending Sync (only valid if has_pending_sync()).
    [[nodiscard]] auto pending_sequence_id() const noexcept -> uint16_t { return pending_.sequence_id; }

  private:
    State state_{State::Idle};

    struct Pending
    {
        uint16_t sequence_id{0};
        SourcePortIdentity source_port_identity{};
        int64_t sync_rx_local_ns{0};
        int64_t sync_correction_raw{0};
        int8_t log_message_interval{0};
    };
    Pending pending_{};
    bool pending_valid_{false};
};

}  // namespace statusbar::gptp
