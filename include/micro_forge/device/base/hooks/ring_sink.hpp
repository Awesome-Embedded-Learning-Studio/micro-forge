#pragma once

#include "micro_forge/device/base/hooks/events.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <vector>

namespace micro_forge::device::hooks {

// Non-blocking collector. Hand slot() to Signal::connect(); events get pushed
// into a fixed-capacity ring in O(1). The simulation hot path never waits on
// a slow consumer — if the ring fills, the OLDEST events are overwritten and
// counted in dropped(). A consumer drains() later (batch, off-path, or on
// another thread).
//
// Single-producer (the sim thread) / single-consumer safe via a relaxed
// atomic head; drain() is meant to be called from one consumer.
template <typename E>
class RingSink {
public:
    explicit RingSink(std::size_t capacity) : buf_(capacity == 0U ? 1U : capacity) {}

    // A slot suitable for Signal<E>::connect().
    std::function<void(const E&)> slot() {
        return [this](const E& e) {
            std::size_t const idx = head_.fetch_add(1U, std::memory_order_relaxed);
            buf_[idx % buf_.size()] = e;
        };
    }

    // Snapshot everything buffered since the last drain, in order. Clears the
    // buffer and accounts for any overflow that happened since.
    std::vector<E> drain() {
        std::size_t const h = head_.load(std::memory_order_relaxed);
        std::size_t const produced = h - tail_;
        std::size_t const avail = produced < buf_.size() ? produced : buf_.size();
        std::vector<E> out;
        out.reserve(avail);
        std::size_t const start = (produced > buf_.size()) ? (h - buf_.size()) : tail_;
        for (std::size_t i = 0U; i < avail; ++i) {
            out.push_back(buf_[(start + i) % buf_.size()]);
        }
        if (produced > buf_.size()) {
            dropped_ += produced - buf_.size();
        }
        tail_ = h;
        return out;
    }

    [[nodiscard]] std::size_t dropped() const noexcept { return dropped_; }

private:
    std::vector<E> buf_;
    std::atomic<std::size_t> head_{0U};
    std::size_t tail_ = 0U;
    std::size_t dropped_ = 0U;
};

} // namespace micro_forge::device::hooks
