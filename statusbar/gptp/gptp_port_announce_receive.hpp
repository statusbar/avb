#pragma once

// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

//
// PortAnnounceReceive — informational-only.
//
// In the slave-only follower we do NOT run BMCA; a grandmaster is
// implied by the network topology (in Automotive Profile) or
// statically known to the application (in Standard profile). But we
// still want to parse incoming Announce messages to give the
// application visibility into the current grandmaster's identity,
// clock class / accuracy / variance, priority fields, UTC offset,
// and time source — all useful for diagnostics and for applications
// that need to know "who are we actually syncing from?"
//
// This class latches the most recent Announce data and expires it
// on announce-receipt timeout. It never makes port-state decisions.
//

#include "statusbar/gptp/gptp_base.hpp"
#include "statusbar/gptp/gptp_messages.hpp"

#include <cstdint>
#include <optional>

namespace statusbar::gptp {

/// Latched grandmaster information extracted from the most recent
/// Announce message.
struct GrandmasterInfo
{
    tsn::ClockIdentity identity{};
    ClockQuality clock_quality{};
    uint8_t priority1{255};
    uint8_t priority2{255};
    int16_t current_utc_offset{0};
    uint16_t steps_removed{0};
    uint8_t time_source{0};
    /// Source port identity from the Announce header (which port the
    /// Announce actually came from — may differ from the GM if the
    /// Announce traversed bridges).
    SourcePortIdentity source_port_identity{};
    /// log2 of the advertised announce interval, for diagnostic
    /// display.
    int8_t log_message_interval{0};
};

class PortAnnounceReceive
{
  public:
    enum class State : uint8_t
    {
        Idle,     ///< no GM latched yet
        Latched,  ///< an Announce has been received and is current
        Discard,  ///< port disabled
    };

    PortAnnounceReceive() = default;

    [[nodiscard]] auto state() const noexcept -> State { return state_; }

    void enable() noexcept
    {
        if (state_ == State::Discard) {
            state_ = State::Idle;
        }
    }

    void disable() noexcept
    {
        state_ = State::Discard;
        latched_.reset();
    }

    /// Latch the information from an incoming Announce. Returns true
    /// if this Announce introduced a new grandmaster identity (useful
    /// for firing an on_grandmaster_change observer callback).
    auto on_announce(AnnounceMessage const& msg) noexcept -> bool
    {
        if (state_ == State::Discard) {
            return false;
        }
        bool const had_previous = latched_.has_value();
        tsn::ClockIdentity const previous_id = had_previous ? latched_->identity : tsn::ClockIdentity{};

        GrandmasterInfo info{};
        info.identity = msg.grandmaster_identity;
        info.clock_quality = msg.grandmaster_clock_quality;
        info.priority1 = msg.grandmaster_priority1.get();
        info.priority2 = msg.grandmaster_priority2.get();
        info.current_utc_offset = static_cast<int16_t>(msg.current_utc_offset.get());
        info.steps_removed = msg.steps_removed.get();
        info.time_source = msg.time_source.get();
        info.source_port_identity = msg.header.source_port_identity;
        info.log_message_interval = static_cast<int8_t>(msg.header.log_message_interval);

        bool const gm_changed = !had_previous || !(info.identity == previous_id);
        latched_ = info;
        state_ = State::Latched;
        return gm_changed;
    }

    /// Called when the announce-receipt timeout fires. The latched
    /// GM info is cleared; observers get an on_grandmaster_change
    /// with a zero identity to signal GM loss.
    auto on_announce_receipt_timeout() noexcept -> bool
    {
        bool const had_gm = latched_.has_value();
        latched_.reset();
        if (state_ != State::Discard) {
            state_ = State::Idle;
        }
        return had_gm;
    }

    /// Get the latest latched grandmaster info, if any.
    [[nodiscard]] auto current() const noexcept -> std::optional<GrandmasterInfo> const& { return latched_; }

    /// Convenience: return the current grandmaster identity, or a
    /// zero ClockIdentity if no Announce has been received.
    [[nodiscard]] auto current_grandmaster_identity() const noexcept -> tsn::ClockIdentity
    {
        return latched_.has_value() ? latched_->identity : tsn::ClockIdentity{};
    }

  private:
    State state_{State::Idle};
    std::optional<GrandmasterInfo> latched_{};
};

}  // namespace statusbar::gptp
