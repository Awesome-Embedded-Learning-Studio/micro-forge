#pragma once

#include "periph/device.hpp"
#include "util/weak_ptr/weak_ptr_factory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace micro_forge::periph {

class NvicPeripheral : public Device {
  public:
    NvicPeripheral() = default;

    // periph::Device
    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    std::string_view name() const noexcept override { return "NVIC"; }

    // Query API (CPU hot path — inline)
    bool has_pending_irq() const {
        // Polled every CPU step on the interrupt hot path (check_and_handle_
        // interrupt). Reuse the highest_priority_pending_irq() cache —
        // invalidated on every ISER/ICER/ISPR/ICPR/IPR mutation via
        // invalidate_cache() — instead of re-scanning 8 ISPR/ISER words each
        // step. 0xFF means "no enabled+pending IRQ", which is exactly the
        // ispr & iser emptiness the scan computed; semantics identical.
        return highest_priority_pending_irq() != 0xFF;
    }

    uint8_t highest_pending_irq() const {
        for (size_t i = 0; i < ispr_.size(); ++i) {
            uint32_t active = ispr_[i] & iser_[i];
            if (active) {
                return static_cast<uint8_t>(i * 32 + __builtin_ctz(active));
            }
        }
        return 0xFF; // no pending
    }

    // Returns the enabled+pending IRQ with the smallest priority value
    // (i.e. the highest priority). Among equal priorities the lowest IRQ
    // number wins. Returns 0xFF if no enabled+pending IRQ exists.
    //
    // Result is cached: a full scan only runs after NVIC state changes
    // (enable/pending/priority writes). The CPU polls this every step, while
    // such changes happen far less often, so the cache turns the hot path into
    // O(1) while keeping the result identical to a fresh scan.
    uint8_t highest_priority_pending_irq() const {
        if (cache_valid_) {
            return cached_best_irq_;
        }
        uint8_t best = 0xFF;
        uint8_t best_pri = 0xFF;
        for (uint16_t i = 0; i < kMaxIrq; ++i) {
            if (get_bit(iser_, static_cast<uint8_t>(i)) &&
                get_bit(ispr_, static_cast<uint8_t>(i))) {
                uint8_t pri = priorities_[i];
                if (pri < best_pri) {
                    best_pri = pri;
                    best = static_cast<uint8_t>(i);
                }
            }
        }
        cached_best_irq_ = best;
        cached_best_pri_ = best_pri;
        cache_valid_ = true;
        return best;
    }

    uint8_t irq_priority(uint8_t irq_n) const {
        if (irq_n >= kMaxIrq) {
            return 0xFF;
        }
        return priorities_[irq_n];
    }

    bool is_enabled(uint8_t irq_n) const {
        return irq_n < kMaxIrq && get_bit(iser_, irq_n);
    }

    // Number of currently-enabled IRQs (popcount of ISER). Backs the NVIC
    // panel of the introspection snapshot; cheap enough for a per-tick read.
    uint16_t enabled_count() const noexcept {
        uint16_t n = 0;
        for (uint32_t word : iser_) {
            n += static_cast<uint16_t>(__builtin_popcount(word));
        }
        return n;
    }

    // External trigger API
    void set_pending(uint8_t irq_n) {
        if (irq_n < kMaxIrq) {
            set_bit(ispr_, irq_n, true);
            invalidate_cache();
        }
    }

    void clear_pending(uint8_t irq_n) {
        if (irq_n < kMaxIrq) {
            set_bit(ispr_, irq_n, false);
            invalidate_cache();
        }
    }

    WeakPtr<NvicPeripheral> GetWeak() { return weak_factory_.GetWeakPtr(); }

  private:
    static constexpr uint8_t kMaxIrq = 240;

    // Any structural change (enable/pending/priority) dirties the
    // highest-priority cache so the next query recomputes from scratch.
    void invalidate_cache() const noexcept { cache_valid_ = false; }

    static bool get_bit(const std::array<uint32_t, 8>& arr, uint8_t irq_n) {
        if (irq_n >= kMaxIrq) {
            return false;
        }
        return (arr[irq_n / 32] >> (irq_n % 32)) & 1;
    }

    static void set_bit(std::array<uint32_t, 8>& arr, uint8_t irq_n, bool val) {
        if (irq_n >= kMaxIrq) {
            return;
        }
        if (val) {
            arr[irq_n / 32] |= (1u << (irq_n % 32));
        } else {
            arr[irq_n / 32] &= ~(1u << (irq_n % 32));
        }
    }

    std::array<uint32_t, 8> iser_{};
    std::array<uint32_t, 8> ispr_{};
    std::array<uint8_t, kMaxIrq> priorities_{};

    // Lazily-computed cache for highest_priority_pending_irq(). mutable because
    // the const query updates it.
    mutable uint8_t cached_best_irq_ = 0xFF;
    mutable uint8_t cached_best_pri_ = 0xFF;
    mutable bool cache_valid_ = false;

    WeakPtrFactory<NvicPeripheral> weak_factory_{this};
};

} // namespace micro_forge::periph
