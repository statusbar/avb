// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_per_source_tracker.hpp"

#include <cstring>
#include <limits>

namespace statusbar::udptun {

namespace {

[[nodiscard]] auto eui_eq(ieee::Eui64 const& a, ieee::Eui64 const& b) noexcept -> bool
{
    auto const sa = a.span();
    auto const sb = b.span();
    return std::memcmp(sa.data(), sb.data(), 8) == 0;
}

void init_state(
    SourceState& st,
    statusbar::stats::AtomicHistogramConfig const& cfg,
    ieee::Eui64 const& sender,
    uint32_t seq,
    int64_t rx_gptp_ns,
    uint32_t interval_us) noexcept
{
    st.sender_eui64 = sender;
    if (!st.stats) {
        st.stats = std::make_unique<LatencyStats>(cfg);
    } else {
        st.stats->histogram.configure(cfg);
        st.stats->time_stats.reset();
    }
    st.first_seq = seq;
    st.last_seq = seq;
    st.first_seen_gptp_ns = rx_gptp_ns;
    st.last_seen_gptp_ns = rx_gptp_ns;
    st.announced_interval_us = interval_us;
    st.received_count = 0;
    st.out_of_order_count = 0;
    st.duplicate_count = 0;
    st.in_use = true;
}

}  // namespace

PerSourceTracker::PerSourceTracker(statusbar::stats::AtomicHistogramConfig const& cfg, size_t max_sources)
    : hist_cfg_{cfg}
    , slots_(max_sources)
{}

auto PerSourceTracker::find_existing(ieee::Eui64 const& s) noexcept -> SourceState*
{
    for (auto& st : slots_) {
        if (st.in_use && eui_eq(st.sender_eui64, s)) {
            return &st;
        }
    }
    return nullptr;
}

auto PerSourceTracker::find_free_or_lru() noexcept -> SourceState*
{
    SourceState* lru = nullptr;
    int64_t lru_seen = std::numeric_limits<int64_t>::max();
    for (auto& st : slots_) {
        if (!st.in_use) {
            return &st;
        }
        if (st.last_seen_gptp_ns < lru_seen) {
            lru_seen = st.last_seen_gptp_ns;
            lru = &st;
        }
    }
    return lru;
}

auto PerSourceTracker::observe(
    ieee::Eui64 sender, uint32_t seq, int64_t latency_ns, int64_t rx_gptp_ns, uint32_t interval_us) noexcept -> SourceState*
{
    if (slots_.empty()) {
        return nullptr;
    }
    SourceState* st = find_existing(sender);
    bool const is_first_observation = (st == nullptr);
    if (is_first_observation) {
        st = find_free_or_lru();
        init_state(*st, hist_cfg_, sender, seq, rx_gptp_ns, interval_us);
    } else {
        // Signed delta on the 32-bit field handles uint32_t wraparound.
        // delta > 0 → forward; delta == 0 → exact duplicate; delta < 0 →
        // out-of-order. The first observation skips this classification
        // because last_seq was just initialized to seq above.
        auto const delta = static_cast<int32_t>(seq - st->last_seq);
        if (delta > 0) {
            st->last_seq = seq;
        } else if (delta == 0) {
            ++st->duplicate_count;
        } else {
            ++st->out_of_order_count;
        }
    }

    st->stats->record(latency_ns);
    st->last_seen_gptp_ns = rx_gptp_ns;
    st->announced_interval_us = interval_us;
    ++st->received_count;
    return st;
}

auto PerSourceTracker::sources() const -> std::span<SourceState const>
{
    return std::span<SourceState const>{slots_};
}

}  // namespace statusbar::udptun
