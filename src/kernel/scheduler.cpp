#include "micro_forge/kernel/scheduler.hpp"

#include <utility>

namespace micro_forge::kernel {

void Scheduler::schedule(Tick at, Action action) {
    queue_.push(Event{.at = at, .seq = next_seq_++, .action = std::move(action)});
}

std::optional<Tick> Scheduler::next_deadline() const {
    if (queue_.empty()) {
        return std::nullopt;
    }
    return queue_.top().at;
}

void Scheduler::dispatch(Tick now) {
    while (!queue_.empty() && queue_.top().at <= now) {
        const Event event = queue_.top();
        queue_.pop();
        event.action();
    }
}

bool Scheduler::empty() const {
    return queue_.empty();
}

} // namespace micro_forge::kernel
