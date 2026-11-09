/**
 * @file scheduler.hpp
 * @brief icount-driven event queue shared by all machine lines.
 * @version 0.2.0
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_set>
#include <vector>

#include "micro_forge/kernel/snapshot.hpp"

namespace micro_forge::kernel {

/**
 * @brief Virtual time in executed instructions (icount).
 *
 * Both machine lines advance time by counting retired instructions, so
 * runs are reproducible regardless of host speed.
 */
using Tick = std::uint64_t;

/**
 * @brief Priority queue of timed callbacks, keyed by icount deadline.
 *
 * Determinism contract: events with the same deadline fire in the order
 * they were scheduled (a monotonic sequence number breaks ties), and
 * dispatch only depends on the sequence of schedule/cancel/dispatch
 * calls. This is what the batch-1 acceptance test (two identical runs
 * produce a bit-identical event trace) locks in.
 *
 * Batch 4 adds cancellation and periodic rearming:
 * - schedule() returns a Handle; cancel() tombstones it lazily (the heap
 *   entry is dropped when it reaches the top), so cancellation never
 *   reorders live events.
 * - schedule_periodic() rearms the event at at + period after each
 *   firing, with a fresh sequence number (order stays well-defined).
 */
class Scheduler {
public:
    /// Callback fired when its deadline is reached.
    using Action = std::function<void()>;

    /// @brief Identity of one scheduled event or periodic chain.
    ///
    /// The sequence number is the natural id (monotonic per scheduler).
    /// A periodic chain keeps one seq for its whole life, so cancelling
    /// its handle kills the pending instance wherever the chain stands.
    struct Handle {
        /// Sequence number handed out by schedule().
        std::uint64_t seq = 0;
    };

    /// One pending event as seen by snapshot code: deadline, identity,
    /// and period (0 = one-shot).
    struct EventInfo {
        /// Deadline in icount.
        Tick at = 0;
        /// Sequence number of the pending instance.
        std::uint64_t seq = 0;
        /// Rearm period; 0 for one-shot events.
        Tick period = 0;
    };

    /**
     * @brief Schedules @p action to fire once @p at instructions have
     * retired.
     *
     * @param[in] at Deadline in icount; may equal the current time to
     *               fire on the next dispatch.
     * @param[in] action Callback; must not schedule further events that
     *                   expire before @p at (deadlines only move forward).
     * @return Handle for cancellation.
     */
    Handle schedule(Tick at, Action action); // handle may be dropped (fire-and-forget)

    /**
     * @brief Schedules @p action to fire at @p first_at and then again
     *        every @p period instructions, until cancelled.
     *
     * Each firing rearms at (deadline + period) keeping the chain's
     * sequence number (a chain holds one live instance at a time), so
     * the (at, seq) order stays well-defined alongside other events.
     *
     * @param[in] first_at First deadline in icount.
     * @param[in] period Rearm distance in icount; must not be zero.
     * @param[in] action Callback fired on every deadline.
     * @return Handle of the first pending instance; cancelling it stops
     *         the whole chain.
     */
    Handle schedule_periodic(Tick first_at, Tick period, Action action);

    /**
     * @brief Cancels the event instance named by @p handle.
     *
     * Cancellation is lazy: the heap entry is dropped when it reaches
     * the top, so live events never reorder. Cancelling an already fired
     * or already cancelled handle is a no-op.
     *
     * @param[in] handle Handle from schedule()/schedule_periodic().
     */
    void cancel(Handle handle);

    /**
     * @brief Earliest pending live deadline.
     *
     * @return The next icount at which a live event fires, or nullopt
     *         when nothing is scheduled.
     */
    [[nodiscard]] std::optional<Tick> next_deadline() const;

    /**
     * @brief Fires every live event whose deadline is at or before @p now.
     *
     * Events fire in deadline order, then in schedule order within the
     * same deadline. Events scheduled from inside a callback join the
     * same pass when their deadline allows. A periodic event rearms into
     * the same pass when its new deadline allows it.
     *
     * @param[in] now Current icount.
     */
    void dispatch(Tick now);

    /// @brief True when no live event is pending.
    [[nodiscard]] bool empty() const;

    /// @brief Live pending events (deadline, sequence, period), ordered
    ///        by (at, seq) -- the snapshot view of the queue.
    [[nodiscard]] std::vector<EventInfo> live_events() const;

    /// @brief The next sequence number handed out on the next schedule;
    ///        snapshot
    ///        restore replays it so restored events keep their ordering.
    [[nodiscard]] std::uint64_t next_seq() const { return next_seq_; }

    /**
     * @brief Serializes next_seq and the live event table (at, seq,
     *        period) into @p writer.
     *
     * @param[in,out] writer Snapshot payload writer.
     */
    void save(SnapshotWriter& writer) const;

    /**
     * @brief Reads back next_seq and the event count.
     *
     * @param[in,out] reader Positioned snapshot reader.
     * @return Event triple count, or nullopt on a short read (the caller
     *         must refuse the blob, not treat it as zero events).
     */
    std::optional<std::uint64_t> restore_begin(SnapshotReader& reader);

    /**
     * @brief Reads one serialized event triple (in save order).
     *
     * @param[in,out] reader Positioned snapshot reader.
     * @return The placement, or nullopt on a short read.
     */
    [[nodiscard]] static std::optional<EventInfo> restore_read_event(SnapshotReader& reader);

    /**
     * @brief Re-registers an event with a preserved identity.
     *
     * Snapshot-restore path only: a device re-attaches its callback to a
     * serialized (at, seq, period) triple. The restored event must fire
     * exactly where the original would have; using this outside restore
     * breaks the monotonic-sequence invariant.
     *
     * @param[in] info Serialized event placement.
     * @param[in] action The device callback.
     */
    void restore_event(EventInfo info, Action action);

private:
    /// One queue entry; period != 0 marks a self-rearming chain.
    struct Event {
        /// Deadline in icount.
        Tick at = 0;
        /// Schedule-order identity.
        std::uint64_t seq = 0;
        /// Rearm period; 0 for one-shot events.
        Tick period = 0;
        /// What fires.
        Action action;
    };

    /// Earlier deadline first; same deadline: earlier schedule first.
    struct Later {
        bool operator()(const Event& a, const Event& b) const {
            return std::make_pair(a.at, a.seq) > std::make_pair(b.at, b.seq);
        }
    };

    /// Heap storage is hand-rolled (push_heap/pop_heap over a vector) so
    /// next_deadline() and live_events() can skip tombstones by scanning.
    std::vector<Event> heap_;
    /// Cached heap-front timestamp: dispatch() is called once per retired
    /// instruction, and an empty/tomorrow heap must cost one compare, not
    /// a heap-front dereference (V8 profile: 2.7% of the wasm run).
    Tick next_at_ = 0;
    /// True when the heap has changed since next_at_ was cached.
    bool next_at_valid_ = false;
    std::unordered_set<std::uint64_t> cancelled_; ///< Tombstoned seqs.
    std::uint64_t next_seq_ = 0;
};

} // namespace micro_forge::kernel
