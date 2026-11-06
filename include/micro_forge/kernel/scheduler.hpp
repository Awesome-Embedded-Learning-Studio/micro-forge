/**
 * @file scheduler.hpp
 * @brief icount-driven event queue shared by all machine lines.
 * @version 0.1.0
 */

#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <queue>
#include <vector>

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
 * dispatch only depends on the sequence of schedule/dispatch calls. This
 * is what the batch-1 acceptance test (two identical runs produce a
 * bit-identical event trace) locks in.
 */
class Scheduler {
public:
    /// Callback fired when its deadline is reached.
    using Action = std::function<void()>;

    /**
     * @brief Schedules @p action to fire once @p at instructions have
     * retired.
     *
     * @param[in] at Deadline in icount; may equal the current time to
     *               fire on the next dispatch.
     * @param[in] action Callback; must not schedule further events that
     *                   expire before @p at (deadlines only move forward).
     */
    void schedule(Tick at, Action action);

    /**
     * @brief Earliest pending deadline.
     *
     * @return The next icount at which an event fires, or nullopt when
     *         nothing is scheduled.
     */
    [[nodiscard]] std::optional<Tick> next_deadline() const;

    /**
     * @brief Fires every event whose deadline is at or before @p now.
     *
     * Events fire in deadline order, then in schedule order within the
     * same deadline. Events scheduled from inside a callback join the
     * same pass when their deadline allows.
     *
     * @param[in] now Current icount.
     */
    void dispatch(Tick now);

    /// @brief True when no event is pending.
    [[nodiscard]] bool empty() const;

private:
    struct Event {
        Tick at;
        std::uint64_t seq; // schedule order, breaks deadline ties
        Action action;
    };

    /// Earlier deadline first; same deadline: earlier schedule first.
    struct Later {
        bool operator()(const Event& a, const Event& b) const {
            return std::make_pair(a.at, a.seq) > std::make_pair(b.at, b.seq);
        }
    };

    std::priority_queue<Event, std::vector<Event>, Later> queue_;
    std::uint64_t next_seq_ = 0;
};

} // namespace micro_forge::kernel
