/**
 * @file scheduler.cpp
 * @brief icount event queue: scheduling, lazy cancellation, periodic
 *        rearming, and snapshot views.
 * @version 0.2.0
 */

#include "micro_forge/kernel/scheduler.hpp"

namespace micro_forge::kernel {

Scheduler::Handle Scheduler::schedule(Tick at, Action action) {
    const std::uint64_t seq = next_seq_++;
    heap_.push_back(Event{
        .at = at,
        .seq = seq,
        .period = 0,
        .action = std::move(action),
    });
    std::ranges::push_heap(heap_.begin(), heap_.end(), Later());
    next_at_valid_ = false;
    return Handle{.seq = seq};
}

Scheduler::Handle Scheduler::schedule_periodic(Tick first_at, Tick period, Action action) {
    if (period == 0) {
        period = 1; // a zero period would spin; degrade, never hang
    }
    const std::uint64_t seq = next_seq_++;
    heap_.push_back(Event{
        .at = first_at,
        .seq = seq,
        .period = period,
        .action = std::move(action),
    });
    std::ranges::push_heap(heap_.begin(), heap_.end(), Later());
    next_at_valid_ = false;
    return Handle{.seq = seq};
}

void Scheduler::cancel(Handle handle) {
    next_at_valid_ = false;
    // Lazy: tombstone now, physically drop when the entry reaches the
    // top (or when next_deadline/live_events scans past it).
    cancelled_.insert(handle.seq);
}

std::optional<Tick> Scheduler::next_deadline() const {
    std::optional<Tick> earliest;
    for (const Event& e : heap_) {
        if (cancelled_.contains(e.seq)) {
            continue;
        }
        if (!earliest.has_value() || e.at < *earliest) {
            earliest = e.at;
        }
    }
    return earliest;
}

void Scheduler::dispatch(Tick now) {
    if (!next_at_valid_) {
        if (heap_.empty()) {
            return;
        }
        next_at_ = heap_.front().at;
        next_at_valid_ = true;
    }
    if (next_at_ > now) {
        return; // earliest live-or-dead entry is not due yet
    }
    for (;;) {
        if (heap_.empty()) {
            next_at_valid_ = false;
            return;
        }
        if (heap_.front().at > now) {
            next_at_ = heap_.front().at;
            return; // earliest live-or-dead entry is not due yet
        }
        std::ranges::pop_heap(heap_, Later());
        const Event fired = heap_.back();
        heap_.pop_back();
        next_at_valid_ = false;
        if (cancelled_.contains(fired.seq)) {
            cancelled_.erase(fired.seq);
            continue; // tombstone reaches the top: drop it
        }
        if (fired.period != 0) {
            // The chain keeps its sequence number: a chain holds exactly
            // one live instance at a time, so the seq identifies the
            // chain, and cancelling the handle kills the pending instance
            // wherever the chain stands (and stops future rearms).
            heap_.push_back(Event{
                .at = fired.at + fired.period,
                .seq = fired.seq,
                .period = fired.period,
                .action = fired.action,
            });
            std::ranges::push_heap(heap_, Later());
        }
        // Fire with the heap already consistent: the callback may
        // schedule or cancel, including this event's chain.
        fired.action();
    }
}

bool Scheduler::empty() const {
    return !next_deadline().has_value();
}

std::vector<Scheduler::EventInfo> Scheduler::live_events() const {
    std::vector<EventInfo> out;
    for (const Event& e : heap_) {
        if (!cancelled_.contains(e.seq)) {
            out.push_back(EventInfo{.at = e.at, .seq = e.seq, .period = e.period});
        }
    }
    std::ranges::sort(out, [](const EventInfo& a, const EventInfo& b) {
        return std::make_pair(a.at, a.seq) < std::make_pair(b.at, b.seq);
    });
    return out;
}

void Scheduler::restore_event(EventInfo info, Action action) {
    heap_.push_back(
        Event{.at = info.at, .seq = info.seq, .period = info.period, .action = std::move(action)});
    std::ranges::push_heap(heap_, Later());
    // The restored sequence must be part of the future, never reused.
    if (info.seq >= next_seq_) {
        next_seq_ = info.seq + 1;
    }
}

void Scheduler::save(SnapshotWriter& writer) const {
    writer.u64(next_seq_);
    const auto live = live_events();
    writer.u64(live.size());
    for (const EventInfo& e : live) {
        writer.u64(e.at);
        writer.u64(e.seq);
        writer.u64(e.period);
    }
}

std::optional<std::uint64_t> Scheduler::restore_begin(SnapshotReader& reader) {
    bool ok = true;
    const std::uint64_t saved_next_seq = reader.u64(ok);
    const std::uint64_t count = reader.u64(ok);
    if (!ok) {
        return std::nullopt; // short read: refuse, never "zero events"
    }
    next_seq_ = saved_next_seq; // restored events keep their identity below
    return count;
}

std::optional<Scheduler::EventInfo> Scheduler::restore_read_event(SnapshotReader& reader) {
    bool ok = true;
    EventInfo info;
    info.at = reader.u64(ok);
    info.seq = reader.u64(ok);
    info.period = reader.u64(ok);
    if (!ok) {
        return std::nullopt;
    }
    return info;
}

} // namespace micro_forge::kernel
