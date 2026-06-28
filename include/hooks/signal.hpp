#pragma once

#include <cstddef>
#include <functional>
#include <vector>

namespace micro_forge::hooks {

// Typed observer signal — the simulation's "hook" point for a class of
// events. Peripherals emit domain events; observers connect slots.
//
// emit() dispatches SYNCHRONOUSLY to every connected slot. The contract is
// that slots stay cheap (push into a RingSink, flip a flag, tally a counter).
// Formatting, IO, network — anything that could stall the hot path — belongs
// in a consumer that drains off-path. That keeps the simulator deterministic
// and the inner loop tight: the "non-blocking" property is enforced by
// discipline at the slot boundary, not by a thread pool.
template <typename E> class Signal {
  public:
    using Slot = std::function<void(const E&)>;
    using Token = std::size_t;

    // Connect a slot; returns a token for disconnect(). Safe to connect
    // before the simulation runs.
    Token connect(Slot slot) {
        slots_.push_back({next_++, std::move(slot)});
        return next_ - 1;
    }

    void disconnect(Token t) {
        for (auto it = slots_.begin(); it != slots_.end(); ++it) {
            if (it->token == t) {
                slots_.erase(it);
                return;
            }
        }
    }

    // Fire the event to every slot, in connect order.
    void emit(const E& event) const {
        for (const auto& s : slots_) {
            s.slot(event);
        }
    }

    bool empty() const noexcept { return slots_.empty(); }

  private:
    struct Entry {
        Token token;
        Slot slot;
    };
    std::vector<Entry> slots_;
    Token next_ = 0;
};

} // namespace micro_forge::hooks
