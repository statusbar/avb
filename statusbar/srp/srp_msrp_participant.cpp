// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

//
// MsrpParticipant — out-of-line template definitions.
//
// Contains the implementation of every MsrpParticipantT<Limits> method
// along with the detail_msrp helper namespace. Explicit instantiation
// of MsrpParticipantT<DefaultMsrpLimits> (the only Limits type used in
// this tree) lives at the bottom; add another template class line here
// if you ever need a second variant.
//

#include "statusbar/srp/srp_msrp_participant.hpp"

#include <algorithm>

namespace statusbar::srp::msrp {

// ============================================================
// Implementation — out-of-line template method definitions
// ============================================================

namespace detail_msrp {

using mrp::AttributeEvent;
using mrp::AttributeListHeader;
using mrp::VectorAttributeHeader;

/// Translate an internal Applicant sndmsg (New/Join/In/Leave) into the
/// wire-level AttributeEvent. The Join->JoinIn/JoinMt and In->In/Mt
/// decision requires knowing the paired Registrar state — see
/// msrp.c:2002-2036.
constexpr auto translate_sndmsg(applicant_sm::SendMessage msg, bool registrar_is_in) noexcept -> AttributeEvent
{
    using applicant_sm::SendMessage;
    switch (msg) {
        case SendMessage::New:
            return AttributeEvent::New;
        case SendMessage::Join:
            return registrar_is_in ? AttributeEvent::JoinIn : AttributeEvent::JoinMt;
        case SendMessage::In:
            return registrar_is_in ? AttributeEvent::In : AttributeEvent::Mt;
        case SendMessage::Leave:
            return AttributeEvent::Lv;
        case SendMessage::None:
        default:
            return AttributeEvent::Mt;  // unreachable in practice; harmless default
    }
}

/// Translate a wire AttributeEvent into the Applicant/Registrar input
/// events that should be dispatched for this receive.
struct RxEvents
{
    applicant_sm::Def::Event applicant;
    registrar_sm::Def::Event registrar;
};

constexpr auto rx_events_for(AttributeEvent event) noexcept -> RxEvents
{
    switch (event) {
        case AttributeEvent::New:
            return {.applicant = applicant_sm::Def::Event::RNew, .registrar = registrar_sm::Def::Event::RNew};
        case AttributeEvent::JoinIn:
            return {.applicant = applicant_sm::Def::Event::RJoinIn, .registrar = registrar_sm::Def::Event::RJoinIn};
        case AttributeEvent::In:
            // Registrar ignores rIn! per Clause 10.7.8 Table 10-4
            return {.applicant = applicant_sm::Def::Event::RIn, .registrar = registrar_sm::Def::Event::Count};
        case AttributeEvent::JoinMt:
            return {.applicant = applicant_sm::Def::Event::RJoinMt, .registrar = registrar_sm::Def::Event::RJoinMt};
        case AttributeEvent::Mt:
            // Registrar ignores rMt! per Clause 10.7.8 Table 10-4
            return {.applicant = applicant_sm::Def::Event::RMt, .registrar = registrar_sm::Def::Event::Count};
        case AttributeEvent::Lv:
            return {.applicant = applicant_sm::Def::Event::RLeave, .registrar = registrar_sm::Def::Event::RLeave};
        default:
            return {.applicant = applicant_sm::Def::Event::Count, .registrar = registrar_sm::Def::Event::Count};
    }
}

/// Increment the tsn::StreamId contained in a TalkerAdvertiseFirstValue by
/// one unique_id step. Used when expanding a multi-value vector
/// attribute during receive.
inline void increment_stream_id(tsn::StreamId& id) noexcept
{
    id.increment_unique_id();
}

/// Generic erase-remove for containers without std::erase_if support
/// (e.g. statusbar::sg14::inplace_vector).
template <typename Container, typename Pred>
void erase_if(Container& c, Pred pred)
{
    c.erase(std::remove_if(c.begin(), c.end(), pred), c.end());
}

/// Reset a MutableBuffer's used-data span to zero length, preserving
/// the underlying storage for reuse across TX passes.
inline void reset_buffer(MutableBuffer& buf) noexcept
{
    buf.set_span(std::span<uint8_t const>(buf.total_buffer_span().data(), 0));
}

/// Append a single byte to a MutableBuffer. Returns false if the
/// buffer has no free space (caller should stop appending and emit
/// whatever has been produced so far).
[[nodiscard]] inline auto append_u8(MutableBuffer& buf, uint8_t value) noexcept -> bool
{
    std::array<uint8_t, 1> const src{value};
    return static_cast<bool>(buf.append(std::span<uint8_t const>(src)));
}

/// Append a big-endian uint16 to a MutableBuffer.
[[nodiscard]] inline auto append_be16(MutableBuffer& buf, uint16_t value) noexcept -> bool
{
    std::array<uint8_t, 2> const src{
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value & 0xFFU),
    };
    return static_cast<bool>(buf.append(std::span<uint8_t const>(src)));
}

}  // namespace detail_msrp

// Convenience using-declarations so method bodies can refer to the
// mrp helpers and detail_msrp helpers unqualified, matching the
// pre-templatization code.
using detail_msrp::append_be16;
using detail_msrp::append_u8;
using detail_msrp::erase_if;
using detail_msrp::increment_stream_id;
using detail_msrp::reset_buffer;
using detail_msrp::rx_events_for;
using detail_msrp::translate_sndmsg;
using mrp::AttributeEvent;
using mrp::AttributeListHeader;
using mrp::VectorAttributeHeader;

// ============================================================
// Construction / lifecycle
// ============================================================

template <class Limits>
MsrpParticipantT<Limits>::MsrpParticipantT(MsrpConfig const& config, uint64_t rng_seed)
    : config_{config}
    , port_{rng_seed}
{
    if (auto const valid = config_.validate(); !valid) {
        throw std::system_error(valid.error());
    }
    // Reserve all dynamic containers up front — no further allocations
    // occur during protocol processing. statusbar::sg14::inplace_vector::reserve()
    // is a static no-op that throws std::bad_alloc if the runtime size
    // exceeds the template capacity, so these calls serve as a runtime
    // capacity check against the MsrpCapacityLimits values.
    // NOLINTBEGIN(readability-static-accessed-through-instance)
    talker_adv_.reserve(config_.max_talker_advertise);
    talker_failed_.reserve(config_.max_talker_failed);
    listeners_.reserve(config_.max_listeners);
    domains_.reserve(config_.max_domains);
    // NOLINTEND(readability-static-accessed-through-instance)
    observers_.resize(config_.max_observers);                             // all slots initially free (id == 0)
    interesting_stream_ids_.reserve(config_.max_interesting_stream_ids);  // NOLINT(readability-static-accessed-through-instance)
}

template <class Limits>
void MsrpParticipantT<Limits>::set_send_pdu(SendPduFn fn)
{
    send_pdu_ = std::move(fn);
}

template <class Limits>
void MsrpParticipantT<Limits>::start(TimePoint now)
{
    port_.start(now);
}

template <class Limits>
void MsrpParticipantT<Limits>::stop() noexcept
{
    port_.stop();
}

// ============================================================
// Observer subscription (slot-based, fixed capacity)
// ============================================================

template <class Limits>
auto MsrpParticipantT<Limits>::subscribe(Observer obs) -> SubscriptionId
{
    for (auto& slot : observers_) {
        if (slot.id == 0) {
            auto const id = next_subscription_id_++;
            // Guard against id==0 wraparound (never hand out the sentinel).
            if (next_subscription_id_ == 0) {
                next_subscription_id_ = 1;
            }
            slot.id = id;
            slot.obs = std::move(obs);
            return id;
        }
    }
    // Observer table full. Return 0 — the caller should check.
    return 0;
}

template <class Limits>
void MsrpParticipantT<Limits>::unsubscribe(SubscriptionId id)
{
    if (id == 0) {
        return;
    }
    for (auto& slot : observers_) {
        if (slot.id == id) {
            slot.id = 0;
            slot.obs = Observer{};
            return;
        }
    }
}

// ============================================================
// Interesting Stream ID pruning (sorted vector, binary searched)
// ============================================================

template <class Limits>
auto MsrpParticipantT<Limits>::add_interesting_stream_id(tsn::StreamId const& id) -> Status
{
    auto const key = id.to_uint64();
    auto* const it = std::lower_bound(interesting_stream_ids_.begin(), interesting_stream_ids_.end(), key);
    if (it != interesting_stream_ids_.end() && *it == key) {
        return success();  // already present
    }
    if (interesting_stream_ids_.size() >= config_.max_interesting_stream_ids) {
        return failure(make_error_code(tsn::TsnError::interesting_table_full));
    }
    interesting_stream_ids_.insert(it, key);
    return success();
}

template <class Limits>
void MsrpParticipantT<Limits>::remove_interesting_stream_id(tsn::StreamId const& id)
{
    auto const key = id.to_uint64();
    auto* const it = std::lower_bound(interesting_stream_ids_.begin(), interesting_stream_ids_.end(), key);
    if (it != interesting_stream_ids_.end() && *it == key) {
        interesting_stream_ids_.erase(it);
    }
}

template <class Limits>
void MsrpParticipantT<Limits>::clear_interesting_stream_ids() noexcept
{
    interesting_stream_ids_.clear();
}

template <class Limits>
auto MsrpParticipantT<Limits>::is_interesting(tsn::StreamId const& id) const noexcept -> bool
{
    if (!pruning_enabled_) {
        return true;
    }
    auto const key = id.to_uint64();
    return std::binary_search(interesting_stream_ids_.begin(), interesting_stream_ids_.end(), key);
}

// ============================================================
// Query API
// ============================================================

template <class Limits>
auto MsrpParticipantT<Limits>::find_talker_advertise(tsn::StreamId const& id) const noexcept -> TalkerAdvertiseFirstValue const*
{
    for (auto const& rec : talker_adv_) {
        if (rec.first_value.stream_id == id) {
            return &rec.first_value;
        }
    }
    return nullptr;
}

template <class Limits>
auto MsrpParticipantT<Limits>::find_talker_failed(tsn::StreamId const& id) const noexcept -> TalkerFailedFirstValue const*
{
    for (auto const& rec : talker_failed_) {
        if (rec.first_value.advertise.stream_id == id) {
            return &rec.first_value;
        }
    }
    return nullptr;
}

template <class Limits>
auto MsrpParticipantT<Limits>::find_listener(tsn::StreamId const& id) const noexcept -> ListenerRecord const*
{
    for (auto const& rec : listeners_) {
        if (rec.first_value.stream_id == id) {
            return &rec;
        }
    }
    return nullptr;
}

template <class Limits>
auto MsrpParticipantT<Limits>::listener_permits_transmit(tsn::StreamId const& stream_id) const noexcept -> bool
{
    auto const* rec = find_listener(stream_id);
    if (rec == nullptr) {
        return false;  // no listener declaration received yet
    }
    // Only peer-observed listener declarations authorize transmit;
    // a locally-declared listener doesn't prove anyone downstream
    // actually wants the stream.
    if (rec->operation != Operation::Register) {
        return false;
    }
    // Must be actually registered (Registrar in "In" state), not
    // pending leave or already gone.
    if (!rec->registrar_is_in()) {
        return false;
    }
    return rec->substate == ListenerDeclaration::Ready || rec->substate == ListenerDeclaration::ReadyFailed;
}

template <class Limits>
auto MsrpParticipantT<Limits>::find_domain(uint8_t sr_class_id) const noexcept -> DomainFirstValue const*
{
    for (auto const& rec : domains_) {
        if (rec.first_value.sr_class_id.get() == sr_class_id) {
            return &rec.first_value;
        }
    }
    return nullptr;
}

// ============================================================
// Find-or-create helpers
// ============================================================

template <class Limits>
auto MsrpParticipantT<Limits>::find_or_create_talker_advertise(tsn::StreamId const& id)
    -> AttributeRecord<TalkerAdvertiseFirstValue>*
{
    for (auto& rec : talker_adv_) {
        if (rec.first_value.stream_id == id) {
            return &rec;
        }
    }
    if (talker_adv_.size() >= config_.max_talker_advertise) {
        return nullptr;
    }
    AttributeRecord<TalkerAdvertiseFirstValue> rec{};
    rec.first_value.stream_id = id;
    talker_adv_.push_back(rec);
    return &talker_adv_.back();
}

template <class Limits>
auto MsrpParticipantT<Limits>::find_or_create_talker_failed(tsn::StreamId const& id) -> AttributeRecord<TalkerFailedFirstValue>*
{
    for (auto& rec : talker_failed_) {
        if (rec.first_value.advertise.stream_id == id) {
            return &rec;
        }
    }
    if (talker_failed_.size() >= config_.max_talker_failed) {
        return nullptr;
    }
    AttributeRecord<TalkerFailedFirstValue> rec{};
    rec.first_value.advertise.stream_id = id;
    talker_failed_.push_back(rec);
    return &talker_failed_.back();
}

template <class Limits>
auto MsrpParticipantT<Limits>::find_or_create_listener(tsn::StreamId const& id) -> ListenerRecord*
{
    for (auto& rec : listeners_) {
        if (rec.first_value.stream_id == id) {
            return &rec;
        }
    }
    if (listeners_.size() >= config_.max_listeners) {
        return nullptr;
    }
    ListenerRecord rec{};
    rec.first_value.stream_id = id;
    listeners_.push_back(rec);
    return &listeners_.back();
}

template <class Limits>
auto MsrpParticipantT<Limits>::find_or_create_domain(uint8_t sr_class_id) -> AttributeRecord<DomainFirstValue>*
{
    for (auto& rec : domains_) {
        if (rec.first_value.sr_class_id.get() == sr_class_id) {
            return &rec;
        }
    }
    if (domains_.size() >= config_.max_domains) {
        return nullptr;
    }
    AttributeRecord<DomainFirstValue> rec{};
    rec.first_value.sr_class_id = sr_class_id;
    domains_.push_back(rec);
    return &domains_.back();
}

// ============================================================
// Local declaration API
// ============================================================

template <class Limits>
auto MsrpParticipantT<Limits>::declare_talker_advertise(TalkerAdvertiseFirstValue const& fv, TimePoint now) -> Status
{
    auto* rec_ptr = find_or_create_talker_advertise(fv.stream_id);
    if (rec_ptr == nullptr) {
        return failure(make_error_code(tsn::TsnError::attribute_table_full));
    }
    auto& rec = *rec_ptr;
    bool const is_new = (rec.operation == Operation::Register) &&
        (rec.applicant_sm.current_state() == applicant_sm::Def::State::Start ||
         rec.applicant_sm.current_state() == applicant_sm::Def::State::Vo);
    rec.first_value = fv;
    rec.operation = Operation::Declare;

    auto const event = is_new ? applicant_sm::Def::Event::New : applicant_sm::Def::Event::Join;
    mrp::dispatch_applicant(rec, event, now);
    port_.timers().start_join(now);
    return success();
}

template <class Limits>
auto MsrpParticipantT<Limits>::declare_talker_failed(TalkerFailedFirstValue const& fv, TimePoint now) -> Status
{
    auto* rec_ptr = find_or_create_talker_failed(fv.advertise.stream_id);
    if (rec_ptr == nullptr) {
        return failure(make_error_code(tsn::TsnError::attribute_table_full));
    }
    auto& rec = *rec_ptr;
    bool const is_new = (rec.operation == Operation::Register) &&
        (rec.applicant_sm.current_state() == applicant_sm::Def::State::Start ||
         rec.applicant_sm.current_state() == applicant_sm::Def::State::Vo);
    rec.first_value = fv;
    rec.operation = Operation::Declare;

    auto const event = is_new ? applicant_sm::Def::Event::New : applicant_sm::Def::Event::Join;
    mrp::dispatch_applicant(rec, event, now);
    port_.timers().start_join(now);
    return success();
}

template <class Limits>
auto MsrpParticipantT<Limits>::withdraw_talker(tsn::StreamId const& stream_id, TimePoint now) -> Status
{
    bool found = false;
    for (auto& rec : talker_adv_) {
        if (rec.first_value.stream_id == stream_id) {
            mrp::dispatch_applicant(rec, applicant_sm::Def::Event::Leave, now);
            found = true;
        }
    }
    for (auto& rec : talker_failed_) {
        if (rec.first_value.advertise.stream_id == stream_id) {
            mrp::dispatch_applicant(rec, applicant_sm::Def::Event::Leave, now);
            found = true;
        }
    }
    if (found) {
        port_.timers().start_join(now);
    }
    return success();
}

template <class Limits>
auto MsrpParticipantT<Limits>::declare_listener(tsn::StreamId const& stream_id, ListenerDeclaration decl, TimePoint now) -> Status
{
    auto* rec_ptr = find_or_create_listener(stream_id);
    if (rec_ptr == nullptr) {
        return failure(make_error_code(tsn::TsnError::attribute_table_full));
    }
    auto& rec = *rec_ptr;
    bool const is_new = (rec.operation == Operation::Register) &&
        (rec.applicant_sm.current_state() == applicant_sm::Def::State::Start ||
         rec.applicant_sm.current_state() == applicant_sm::Def::State::Vo);
    rec.operation = Operation::Declare;
    rec.substate = decl;

    auto const event = is_new ? applicant_sm::Def::Event::New : applicant_sm::Def::Event::Join;
    rec.applicant_ctx.clear_outputs();
    rec.applicant_sm.handle_event(rec.applicant_ctx, event, now);
    port_.timers().start_join(now);
    return success();
}

template <class Limits>
auto MsrpParticipantT<Limits>::withdraw_listener(tsn::StreamId const& stream_id, TimePoint now) -> Status
{
    for (auto& rec : listeners_) {
        if (rec.first_value.stream_id == stream_id) {
            rec.applicant_ctx.clear_outputs();
            rec.applicant_sm.handle_event(rec.applicant_ctx, applicant_sm::Def::Event::Leave, now);
            port_.timers().start_join(now);
            return success();
        }
    }
    return success();  // withdraw of unknown listener is not an error
}

template <class Limits>
auto MsrpParticipantT<Limits>::declare_domain(DomainFirstValue const& fv, TimePoint now) -> Status
{
    auto* rec_ptr = find_or_create_domain(fv.sr_class_id.get());
    if (rec_ptr == nullptr) {
        return failure(make_error_code(tsn::TsnError::attribute_table_full));
    }
    auto& rec = *rec_ptr;
    bool const is_new = (rec.operation == Operation::Register) &&
        (rec.applicant_sm.current_state() == applicant_sm::Def::State::Start ||
         rec.applicant_sm.current_state() == applicant_sm::Def::State::Vo);
    rec.first_value = fv;
    rec.operation = Operation::Declare;

    auto const event = is_new ? applicant_sm::Def::Event::New : applicant_sm::Def::Event::Join;
    mrp::dispatch_applicant(rec, event, now);
    port_.timers().start_join(now);
    return success();
}

template <class Limits>
auto MsrpParticipantT<Limits>::withdraw_domain(uint8_t sr_class_id, TimePoint now) -> Status
{
    for (auto& rec : domains_) {
        if (rec.first_value.sr_class_id.get() == sr_class_id) {
            mrp::dispatch_applicant(rec, applicant_sm::Def::Event::Leave, now);
            port_.timers().start_join(now);
            return success();
        }
    }
    return success();
}

// ============================================================
// Event loop — tick / next_deadline
// ============================================================

template <class Limits>
auto MsrpParticipantT<Limits>::next_deadline() const noexcept -> TimePoint
{
    return port_.timers().next_deadline();
}

template <class Limits>
void MsrpParticipantT<Limits>::tick(TimePoint now)
{
    auto const expired = port_.timers().tick(now);
    if (!expired.any()) {
        return;
    }
    // Order matches mrp.c's dispatch loop ordering: LeaveAll first
    // (triggers TxLA events on all attributes), then Leave (Registrar
    // expiry), then Periodic, then Join (actual transmit).
    if (expired.leaveall) {
        on_leaveall_timer(now);
    }
    if (expired.leave) {
        on_leave_timer(now);
    }
    if (expired.periodic) {
        on_periodic_timer(now);
    }
    if (expired.join) {
        on_join_timer(now);
    }
    reclaim();
}

template <class Limits>
void MsrpParticipantT<Limits>::on_join_timer(TimePoint now)
{
    build_and_send_pdu(now);
}

template <class Limits>
void MsrpParticipantT<Limits>::on_leave_timer(TimePoint now)
{
    // Fire LvTimer event into every attribute's Registrar. Any that
    // were in Lv state transition to Mt and notify Leave.
    auto drive = [&](auto& records, auto&& notify_leave_id) {
        for (auto& rec : records) {
            if (rec.registrar_sm.current_state() != registrar_sm::Def::State::Lv) {
                continue;
            }
            mrp::dispatch_registrar(rec, registrar_sm::Def::Event::LvTimer, now, port_.timers());
            if (rec.registrar_ctx.notify == registrar_sm::Notify::Leave) {
                notify_leave_id(rec);
            }
        }
    };
    drive(talker_adv_, [&](auto const& rec) { notify_talker_leave(rec.first_value.stream_id); });
    drive(talker_failed_, [&](auto const& rec) { notify_talker_leave(rec.first_value.advertise.stream_id); });
    drive(listeners_, [&](auto const& rec) { notify_listener_leave(rec.first_value.stream_id); });
    drive(domains_, [&](auto const& rec) { notify_domain_leave(rec.first_value); });
}

template <class Limits>
void MsrpParticipantT<Limits>::on_leaveall_timer(TimePoint now)
{
    // LeaveAll timer expired: drive the LeaveAll FSM, which will
    // rearm its own timer, then fire TxLeaveAll on every per-attribute
    // Applicant and Registrar FSM. After this, the next TX pass will
    // include the LeaveAll flag in the outgoing PDU.
    port_.dispatch_leaveall(mrp::leaveall_sm::Def::Event::LvaTimer, now);

    auto drive = [&](auto& records) {
        for (auto& rec : records) {
            mrp::dispatch_applicant(rec, applicant_sm::Def::Event::TxLeaveAll, now);
            mrp::dispatch_registrar(rec, registrar_sm::Def::Event::TxLeaveAll, now, port_.timers());
        }
    };
    drive(talker_adv_);
    drive(talker_failed_);
    drive(listeners_);
    drive(domains_);

    // The LeaveAll FSM is now in Active; dispatch Tx so it transitions
    // back to Passive with tx_leaveall_pending=true, and emit the PDU.
    port_.dispatch_leaveall(mrp::leaveall_sm::Def::Event::Tx, now);
    build_and_send_pdu(now);
}

template <class Limits>
void MsrpParticipantT<Limits>::on_periodic_timer(TimePoint now)
{
    // Rearm the Periodic FSM (it requests a timer restart while Active).
    port_.dispatch_periodic(mrp::periodic_sm::Def::Event::Periodic, now);

    // Fire Periodic on every attribute's Applicant.
    auto drive = [&](auto& records) {
        for (auto& rec : records) {
            mrp::dispatch_applicant(rec, applicant_sm::Def::Event::Periodic, now);
        }
    };
    drive(talker_adv_);
    drive(talker_failed_);
    drive(listeners_);
    drive(domains_);

    // Periodic state transitions (Qa -> Aa, Qp -> Ap) may imply tx;
    // schedule the join timer to produce a PDU opportunity shortly.
    port_.timers().start_join(now);
}

// ============================================================
// Observer notifications
// ============================================================

template <class Limits>
void MsrpParticipantT<Limits>::notify_talker_advertise(TalkerAdvertiseFirstValue const& fv, Operation op)
{
    for (auto const& slot : observers_) {
        if (slot.id != 0 && slot.obs.on_talker_advertise) {
            slot.obs.on_talker_advertise(fv, op);
        }
    }
}

template <class Limits>
void MsrpParticipantT<Limits>::notify_talker_failed(TalkerFailedFirstValue const& fv, Operation op)
{
    for (auto const& slot : observers_) {
        if (slot.id != 0 && slot.obs.on_talker_failed) {
            slot.obs.on_talker_failed(fv, op);
        }
    }
}

template <class Limits>
void MsrpParticipantT<Limits>::notify_talker_leave(tsn::StreamId const& id)
{
    for (auto const& slot : observers_) {
        if (slot.id != 0 && slot.obs.on_talker_leave) {
            slot.obs.on_talker_leave(id);
        }
    }
}

template <class Limits>
void MsrpParticipantT<Limits>::notify_listener(tsn::StreamId const& id, ListenerDeclaration decl, Operation op)
{
    for (auto const& slot : observers_) {
        if (slot.id != 0 && slot.obs.on_listener) {
            slot.obs.on_listener(id, decl, op);
        }
    }
}

template <class Limits>
void MsrpParticipantT<Limits>::notify_listener_leave(tsn::StreamId const& id)
{
    for (auto const& slot : observers_) {
        if (slot.id != 0 && slot.obs.on_listener_leave) {
            slot.obs.on_listener_leave(id);
        }
    }
}

template <class Limits>
void MsrpParticipantT<Limits>::notify_domain(DomainFirstValue const& fv, Operation op)
{
    for (auto const& slot : observers_) {
        if (slot.id != 0 && slot.obs.on_domain) {
            slot.obs.on_domain(fv, op);
        }
    }
}

template <class Limits>
void MsrpParticipantT<Limits>::notify_domain_leave(DomainFirstValue const& fv)
{
    for (auto const& slot : observers_) {
        if (slot.id != 0 && slot.obs.on_domain_leave) {
            slot.obs.on_domain_leave(fv);
        }
    }
}

// ============================================================
// Reclaim
// ============================================================

template <class Limits>
void MsrpParticipantT<Limits>::reclaim()
{
    auto is_dead = [](auto const& rec) { return rec.is_dead(); };
    detail_msrp::erase_if(talker_adv_, is_dead);
    detail_msrp::erase_if(talker_failed_, is_dead);
    detail_msrp::erase_if(listeners_, is_dead);
    detail_msrp::erase_if(domains_, is_dead);
}

// ============================================================
// PDU decode
// ============================================================

template <class Limits>
void MsrpParticipantT<Limits>::receive_pdu(std::span<uint8_t const> pdu, TimePoint now)
{
    if (pdu.size() < 3) {  // minimum: version + EndMark
        return;
    }
    if (pdu[0] != PROTOCOL_VERSION) {
        return;  // unknown protocol version
    }

    size_t pos = 1;
    while (pos + 2 <= pdu.size()) {
        uint16_t const end_check = (static_cast<uint16_t>(pdu[pos]) << 8) | pdu[pos + 1];
        if (end_check == mrp::END_MARK) {
            break;
        }

        if (pos + AttributeListHeader::LENGTH + 2 > pdu.size()) {
            return;
        }
        AttributeListHeader hdr;
        (void)load_unchecked(pdu.subspan(pos), &hdr);
        pos += AttributeListHeader::LENGTH;

        // AttributeListLength (IEEE 802.1Q-2014 Clause 10.8.2.3): length in
        // octets of the AttributeList (vectors + trailing EndMark). Consumed
        // here and used to bound the per-message decode scope so a malformed
        // length can't make us read past the message.
        uint16_t const attr_list_length = (static_cast<uint16_t>(pdu[pos]) << 8) | pdu[pos + 1];
        pos += 2;

        size_t const attr_list_end = pos + attr_list_length;
        if (attr_list_end > pdu.size()) {
            return;
        }

        auto const attr_type = static_cast<AttributeType>(hdr.attribute_type.get());
        uint8_t const attr_length = hdr.attribute_length.get();

        auto const message_payload = pdu.first(attr_list_end);
        decode_attribute_list(attr_type, attr_length, message_payload, pos, now);

        // Skip over any trailing bytes the inner decoder didn't consume,
        // keeping the outer message-walk cursor aligned with the sender.
        pos = attr_list_end;
    }

    // After processing received events, a join timer opportunity may
    // be needed so that state transitions triggered by rx events can
    // be transmitted.
    port_.timers().start_join(now);
}

template <class Limits>
void MsrpParticipantT<Limits>::decode_attribute_list(
    AttributeType attr_type, uint8_t attr_length, std::span<uint8_t const> payload, size_t& pos, TimePoint now)
{
    while (pos + 2 <= payload.size()) {
        uint16_t const end_check = (static_cast<uint16_t>(payload[pos]) << 8) | payload[pos + 1];
        if (end_check == mrp::END_MARK) {
            pos += 2;
            return;
        }
        decode_vector_attribute(attr_type, attr_length, payload, pos, now);
    }
}

template <class Limits>
void MsrpParticipantT<Limits>::decode_vector_attribute(
    AttributeType attr_type, uint8_t attr_length, std::span<uint8_t const> payload, size_t& pos, TimePoint now)
{
    if (pos + VectorAttributeHeader::LENGTH > payload.size()) {
        return;
    }
    VectorAttributeHeader vec_hdr;
    (void)load_unchecked(payload.subspan(pos), &vec_hdr);
    pos += VectorAttributeHeader::LENGTH;

    bool const leave_all = vec_hdr.get_leave_all();
    uint16_t const num_values = vec_hdr.get_number_of_values();

    // If the vector carries a LeaveAll flag, deliver it to every
    // attribute of this type.
    if (leave_all) {
        auto drive_leaveall = [&](auto& records) {
            for (auto& rec : records) {
                mrp::dispatch_applicant(rec, applicant_sm::Def::Event::RLeaveAll, now);
                mrp::dispatch_registrar(rec, registrar_sm::Def::Event::RLeaveAll, now, port_.timers());
            }
        };
        switch (attr_type) {
            case AttributeType::TalkerAdvertise:
                drive_leaveall(talker_adv_);
                break;
            case AttributeType::TalkerFailed:
                drive_leaveall(talker_failed_);
                break;
            case AttributeType::Listener:
                drive_leaveall(listeners_);
                break;
            case AttributeType::Domain:
                drive_leaveall(domains_);
                break;
        }
        // Also ack the LeaveAll into our LeaveAll FSM to restart its timer.
        port_.dispatch_leaveall(mrp::leaveall_sm::Def::Event::RLeaveAll, now);
    }

    if (pos + attr_length > payload.size()) {
        return;
    }
    auto const first_value_bytes = payload.subspan(pos, attr_length);
    pos += attr_length;

    size_t const num_event_octets = mrp::threepacked_octet_count(num_values);
    bool const has_declarations = (attr_type == AttributeType::Listener);
    size_t const num_decl_octets = has_declarations ? mrp::fourpacked_octet_count(num_values) : 0;

    if (pos + num_event_octets + num_decl_octets > payload.size()) {
        return;
    }
    auto const event_bytes = payload.subspan(pos, num_event_octets);
    auto const decl_bytes =
        has_declarations ? payload.subspan(pos + num_event_octets, num_decl_octets) : std::span<uint8_t const>{};
    pos += num_event_octets + num_decl_octets;

    // Walk each of the num_values attributes in this vector.
    for (uint16_t i = 0; i < num_values; ++i) {
        auto const unpacked = mrp::unpack3_events(event_bytes[i / 3]);
        AttributeEvent event{};
        switch (i % 3) {
            case 0:
                event = unpacked.first;
                break;
            case 1:
                event = unpacked.second;
                break;
            case 2:
                event = unpacked.third;
                break;
            default:
                break;  // unreachable (i % 3 is always 0..2)
        }

        ListenerDeclaration decl = ListenerDeclaration::Ignore;
        if (has_declarations) {
            auto const four = mrp::unpack4_declarations(decl_bytes[i / 4]);
            uint8_t raw = 0;
            switch (i % 4) {
                case 0:
                    raw = four.first;
                    break;
                case 1:
                    raw = four.second;
                    break;
                case 2:
                    raw = four.third;
                    break;
                case 3:
                    raw = four.fourth;
                    break;
                default:
                    break;  // unreachable (i % 4 is always 0..3)
            }
            decl = static_cast<ListenerDeclaration>(raw);
        }

        handle_rx_event(attr_type, attr_length, first_value_bytes, i, event, decl, now);
    }
}

template <class Limits>
void MsrpParticipantT<Limits>::handle_rx_event(
    AttributeType attr_type,
    uint8_t attr_length,
    std::span<uint8_t const> first_value_bytes,
    uint16_t value_index,
    AttributeEvent event,
    ListenerDeclaration decl,
    TimePoint now)
{
    auto const rx = detail_msrp::rx_events_for(event);
    if (rx.applicant == applicant_sm::Def::Event::Count) {
        return;  // unknown event
    }
    switch (attr_type) {
        case AttributeType::TalkerAdvertise:
            handle_talker_advertise_rx(attr_length, first_value_bytes, value_index, rx.applicant, rx.registrar, now);
            break;
        case AttributeType::TalkerFailed:
            handle_talker_failed_rx(attr_length, first_value_bytes, value_index, rx.applicant, rx.registrar, now);
            break;
        case AttributeType::Listener:
            handle_listener_rx(attr_length, first_value_bytes, value_index, rx.applicant, rx.registrar, decl, now);
            break;
        case AttributeType::Domain:
            handle_domain_rx(attr_length, first_value_bytes, value_index, rx.applicant, rx.registrar, now);
            break;
    }
}

template <class Limits>
void MsrpParticipantT<Limits>::handle_talker_advertise_rx(
    uint8_t attr_length,
    std::span<uint8_t const> first_value_bytes,
    uint16_t value_index,
    applicant_sm::Def::Event applicant_event,
    registrar_sm::Def::Event registrar_event,
    TimePoint now)
{
    if (attr_length < TalkerAdvertiseFirstValue::LENGTH) {
        return;
    }
    TalkerAdvertiseFirstValue fv;
    (void)load_unchecked(first_value_bytes, &fv);
    for (uint16_t k = 0; k < value_index; ++k) {
        detail_msrp::increment_stream_id(fv.stream_id);
    }
    if (!is_interesting(fv.stream_id)) {
        return;
    }
    auto* rec_ptr = find_or_create_talker_advertise(fv.stream_id);
    if (rec_ptr == nullptr) {
        return;  // peer attribute, table full — silently drop
    }
    auto& rec = *rec_ptr;
    bool const was_registered = (rec.registrar_sm.current_state() == registrar_sm::Def::State::In);
    rec.first_value = fv;
    if (rec.operation == Operation::Register) {
        // only overwrite operation if not a local declaration
        rec.operation = Operation::Register;
    }
    mrp::dispatch_applicant(rec, applicant_event, now);
    if (registrar_event == registrar_sm::Def::Event::Count) {
        return;
    }
    mrp::dispatch_registrar(rec, registrar_event, now, port_.timers());
    auto const n = rec.registrar_ctx.notify;
    if (n == registrar_sm::Notify::New || n == registrar_sm::Notify::Join) {
        notify_talker_advertise(rec.first_value, Operation::Register);
    } else if (n == registrar_sm::Notify::Leave && was_registered) {
        notify_talker_leave(rec.first_value.stream_id);
    }
}

template <class Limits>
void MsrpParticipantT<Limits>::handle_talker_failed_rx(
    uint8_t attr_length,
    std::span<uint8_t const> first_value_bytes,
    uint16_t value_index,
    applicant_sm::Def::Event applicant_event,
    registrar_sm::Def::Event registrar_event,
    TimePoint now)
{
    if (attr_length < TalkerFailedFirstValue::LENGTH) {
        return;
    }
    TalkerFailedFirstValue fv;
    (void)load_unchecked(first_value_bytes, &fv);
    for (uint16_t k = 0; k < value_index; ++k) {
        detail_msrp::increment_stream_id(fv.advertise.stream_id);
    }
    if (!is_interesting(fv.advertise.stream_id)) {
        return;
    }
    auto* rec_ptr = find_or_create_talker_failed(fv.advertise.stream_id);
    if (rec_ptr == nullptr) {
        return;  // peer attribute, table full — silently drop
    }
    auto& rec = *rec_ptr;
    bool const was_registered = (rec.registrar_sm.current_state() == registrar_sm::Def::State::In);
    rec.first_value = fv;
    mrp::dispatch_applicant(rec, applicant_event, now);
    if (registrar_event == registrar_sm::Def::Event::Count) {
        return;
    }
    mrp::dispatch_registrar(rec, registrar_event, now, port_.timers());
    auto const n = rec.registrar_ctx.notify;
    if (n == registrar_sm::Notify::New || n == registrar_sm::Notify::Join) {
        notify_talker_failed(rec.first_value, Operation::Register);
    } else if (n == registrar_sm::Notify::Leave && was_registered) {
        notify_talker_leave(rec.first_value.advertise.stream_id);
    }
}

template <class Limits>
void MsrpParticipantT<Limits>::handle_listener_rx(
    uint8_t attr_length,
    std::span<uint8_t const> first_value_bytes,
    uint16_t value_index,
    applicant_sm::Def::Event applicant_event,
    registrar_sm::Def::Event registrar_event,
    ListenerDeclaration decl,
    TimePoint now)
{
    if (attr_length < ListenerFirstValue::LENGTH) {
        return;
    }
    ListenerFirstValue fv;
    (void)load_unchecked(first_value_bytes, &fv);
    for (uint16_t k = 0; k < value_index; ++k) {
        detail_msrp::increment_stream_id(fv.stream_id);
    }
    auto* rec_ptr = find_or_create_listener(fv.stream_id);
    if (rec_ptr == nullptr) {
        return;  // peer attribute, table full — silently drop
    }
    auto& rec = *rec_ptr;
    bool const was_registered = (rec.registrar_sm.current_state() == registrar_sm::Def::State::In);
    rec.first_value = fv;
    auto const prev_substate = rec.substate;
    rec.substate = decl;

    rec.applicant_ctx.clear_outputs();
    rec.applicant_sm.handle_event(rec.applicant_ctx, applicant_event, now);
    if (registrar_event == registrar_sm::Def::Event::Count) {
        return;
    }
    rec.registrar_ctx.clear_outputs();
    rec.registrar_sm.handle_event(rec.registrar_ctx, registrar_event, now);
    if (rec.registrar_ctx.lvtimer_request) {
        port_.timers().start_leave(now);
    }
    auto const n = rec.registrar_ctx.notify;
    if (n == registrar_sm::Notify::New || n == registrar_sm::Notify::Join || (was_registered && prev_substate != decl)) {
        notify_listener(rec.first_value.stream_id, decl, Operation::Register);
    } else if (n == registrar_sm::Notify::Leave && was_registered) {
        notify_listener_leave(rec.first_value.stream_id);
    }
}

template <class Limits>
void MsrpParticipantT<Limits>::handle_domain_rx(
    uint8_t attr_length,
    std::span<uint8_t const> first_value_bytes,
    uint16_t value_index,
    applicant_sm::Def::Event applicant_event,
    registrar_sm::Def::Event registrar_event,
    TimePoint now)
{
    if (attr_length < DomainFirstValue::LENGTH) {
        return;
    }
    DomainFirstValue fv;
    (void)load_unchecked(first_value_bytes, &fv);
    // Domain vectors with num_values > 1 would increment the
    // sr_class_id and sr_class_priority fields (see mrp.c), but
    // in practice domains are transmitted as singletons. For
    // multi-value receive we'd need to derive the extra values
    // here; we treat index 0 only for now.
    if (value_index != 0) {
        return;
    }
    auto* rec_ptr = find_or_create_domain(fv.sr_class_id.get());
    if (rec_ptr == nullptr) {
        return;  // peer attribute, table full — silently drop
    }
    auto& rec = *rec_ptr;
    bool const was_registered = (rec.registrar_sm.current_state() == registrar_sm::Def::State::In);
    rec.first_value = fv;
    mrp::dispatch_applicant(rec, applicant_event, now);
    if (registrar_event == registrar_sm::Def::Event::Count) {
        return;
    }
    mrp::dispatch_registrar(rec, registrar_event, now, port_.timers());
    auto const n = rec.registrar_ctx.notify;
    if (n == registrar_sm::Notify::New || n == registrar_sm::Notify::Join) {
        notify_domain(rec.first_value, Operation::Register);
    } else if (n == registrar_sm::Notify::Leave && was_registered) {
        notify_domain_leave(rec.first_value);
    }
}

// ============================================================
// PDU encode
// ============================================================

template <class Limits>
template <typename RecordVec>
auto MsrpParticipantT<Limits>::append_attribute_message(
    MutableBuffer& out, RecordVec& records, AttributeType attr_type, uint8_t attr_length, bool include_leave_all) -> bool
{
    // Count how many records actually want to tx in this pass.
    size_t emit_count = 0;
    for (auto const& rec : records) {
        if (rec.applicant_ctx.tx_pending) {
            ++emit_count;
        }
    }

    // If no attributes want to tx and we don't need to broadcast a
    // LeaveAll, skip the whole message.
    if (emit_count == 0 && !include_leave_all) {
        return false;
    }

    bool const is_listener_attr = (attr_type == AttributeType::Listener);

    // Pre-compute AttributeListLength (IEEE 802.1Q-2014 Clause 10.8.2.3):
    // byte count of the VectorAttributes plus the trailing EndMark (2 octets).
    // Each singleton vector costs: VectorHeader(2) + FirstValue(attr_length)
    // + ThreePackedEvents(1) [+ FourPackedDeclarations(1) for Listener].
    size_t const per_vector_bytes = size_t{2} + size_t{attr_length} + size_t{1} + (is_listener_attr ? size_t{1} : size_t{0});
    size_t attr_list_length = size_t{2};  // trailing EndMark
    if (emit_count > 0) {
        attr_list_length += emit_count * per_vector_bytes;
    } else if (include_leave_all) {
        // Empty-vector LeaveAll: VectorHeader(2) only, no FirstValue / events.
        attr_list_length += size_t{2};
    }
    if (attr_list_length > 0xFFFFU) {
        return false;
    }

    // AttributeListHeader: attribute_type, attribute_length, attribute_list_length
    if (!append_u8(out, static_cast<uint8_t>(attr_type))) {
        return false;
    }
    if (!append_u8(out, attr_length)) {
        return false;
    }
    if (!append_be16(out, static_cast<uint16_t>(attr_list_length))) {
        return false;
    }

    // Emit either a single empty LeaveAll vector (if LVA needed but
    // no attributes to tx) or a series of singleton vectors (one per
    // attribute that wants to tx, with LVA flag attached to the first
    // one if needed).
    bool leave_all_emitted = !include_leave_all;
    bool any_vector_emitted = false;

    for (auto& rec : records) {
        if (!rec.applicant_ctx.tx_pending) {
            continue;
        }
        // Translate sndmsg -> wire AttributeEvent using current Registrar state.
        bool reg_in = false;
        if constexpr (requires { rec.registrar_is_in(); }) {
            reg_in = rec.registrar_is_in();
        }
        auto const wire_event = detail_msrp::translate_sndmsg(rec.applicant_ctx.send_msg, reg_in);

        // Serialize the FirstValue into a stack buffer.
        std::array<uint8_t, 64> fv_buf{};  // 34 bytes is the largest (TalkerFailed)
        auto const fv_span = std::span<uint8_t>(fv_buf.data(), attr_length);
        (void)store_unchecked(fv_span, rec.first_value);

        ListenerDeclaration const decl = [&]() {
            if constexpr (requires { rec.substate; }) {
                return rec.substate;
            } else {
                return ListenerDeclaration::Ignore;
            }
        }();

        // VectorAttributeHeader (2 bytes): leave_all_flag | num_values=1
        VectorAttributeHeader vh{};
        vh.set(!leave_all_emitted, 1);
        if (!append_be16(out, vh.vector_header.get())) {
            return any_vector_emitted;
        }
        // FirstValue
        if (!static_cast<bool>(out.append(std::span<uint8_t const>(fv_span.data(), attr_length)))) {
            return any_vector_emitted;
        }
        // ThreePackedEvents: single value packed with two Mt fillers
        if (!append_u8(out, mrp::pack3_events(wire_event, AttributeEvent::Mt, AttributeEvent::Mt))) {
            return any_vector_emitted;
        }
        // FourPackedDeclarations (Listener only)
        if (is_listener_attr) {
            if (!append_u8(out, mrp::pack4_declarations(static_cast<uint8_t>(decl), 0, 0, 0))) {
                return any_vector_emitted;
            }
        }

        leave_all_emitted = true;
        any_vector_emitted = true;

        // Clear tx_pending for this pass so the next build doesn't re-emit it.
        rec.applicant_ctx.tx_pending = false;
        rec.applicant_ctx.send_msg = applicant_sm::SendMessage::None;
        rec.applicant_ctx.encode = applicant_sm::Encoding::None;
    }

    // If we needed to emit a LeaveAll but no attributes produced a tx,
    // synthesize an empty-vector LeaveAll. This is the "LeaveAll-only"
    // PDU case used on LvaTimer expiry when the database is empty.
    if (include_leave_all && !any_vector_emitted) {
        VectorAttributeHeader vh{};
        vh.set(true, 0);  // leave_all flag set, num_values = 0
        if (!append_be16(out, vh.vector_header.get())) {
            return false;
        }
    }

    // VectorAttribute list EndMark
    if (!append_be16(out, mrp::END_MARK)) {
        return any_vector_emitted || include_leave_all;
    }
    return true;
}

template <class Limits>
void MsrpParticipantT<Limits>::build_and_send_pdu(TimePoint now)
{
    // First, dispatch the appropriate TX event to every attribute's
    // Applicant to generate tx outputs. We pick TxRegistrarIn vs
    // TxRegistrarMt per-attribute based on the paired Registrar's
    // state (Clause 10.7.7 Note 8).
    auto drive_tx = [&](auto& records) {
        for (auto& rec : records) {
            mrp::dispatch_applicant_tx(rec, now);
        }
    };
    drive_tx(talker_adv_);
    drive_tx(talker_failed_);
    drive_tx(listeners_);
    drive_tx(domains_);

    // Did the LeaveAll FSM ask us to tag this PDU?
    bool const leave_all_flag = false;  // Set in on_leaveall_timer path via separate codepath below

    // Reuse the participant's pre-allocated PDU buffer; no per-tx heap.
    reset_buffer(pdu_buffer_);
    if (!append_u8(pdu_buffer_, PROTOCOL_VERSION)) {
        return;
    }

    bool any_message = false;
    any_message |= append_attribute_message(
        pdu_buffer_,
        talker_adv_,
        AttributeType::TalkerAdvertise,
        static_cast<uint8_t>(AttributeLength::TalkerAdvertise),
        leave_all_flag);
    any_message |= append_attribute_message(
        pdu_buffer_,
        talker_failed_,
        AttributeType::TalkerFailed,
        static_cast<uint8_t>(AttributeLength::TalkerFailed),
        leave_all_flag);
    any_message |= append_attribute_message(
        pdu_buffer_, listeners_, AttributeType::Listener, static_cast<uint8_t>(AttributeLength::Listener), leave_all_flag);
    any_message |= append_attribute_message(
        pdu_buffer_, domains_, AttributeType::Domain, static_cast<uint8_t>(AttributeLength::Domain), leave_all_flag);

    if (!any_message) {
        return;
    }
    // Final message EndMark
    (void)append_be16(pdu_buffer_, mrp::END_MARK);

    if (send_pdu_) {
        (void)send_pdu_(pdu_buffer_.get_span());
    }

    (void)now;
}

// ============================================================
// Explicit instantiation
// ============================================================

template class MsrpParticipantT<DefaultMsrpLimits>;

}  // namespace statusbar::srp::msrp
