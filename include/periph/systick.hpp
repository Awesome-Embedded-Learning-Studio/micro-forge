#pragma once

#include "periph/device.hpp"
#include "util/weak_ptr/weak_ptr_factory.hpp"

#include <cstdint>
#include <functional>

namespace micro_forge::periph {

class SysTickPeripheral : public Device {
  public:
    SysTickPeripheral() = default;

    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    void tick(uint64_t cycles) override;
    std::string_view name() const noexcept override { return "SysTick"; }

    WeakPtr<SysTickPeripheral> GetWeak() { return weak_factory_.GetWeakPtr(); }

    void set_irq_callback(std::function<void()> cb) { irq_cb_ = std::move(cb); }

    // Read-only accessors backing the SysTick panel of the introspection
    // snapshot (milestone 04 GUI). COUNTFLAG lives in ctrl bit 16.
    uint32_t ctrl() const noexcept { return ctrl_; }
    uint32_t load() const noexcept { return load_; }
    uint32_t val() const noexcept { return val_; }

    // P1 event-driven timer (emu_busy_wait_research P1/P2): CPU cycles until
    // the next COUNTFLAG reload (+ optional TICKINT), or 0 if the timer can't
    // fire (disabled or load==0). Lets a coordinator skip a busy-wait by
    // advancing straight to the next trigger instead of stepping the poll
    // loop. Read-only — does not change tick() behavior.
    uint64_t cycles_until_next_tick() const noexcept {
        if (!(ctrl_ & 0x1u) || load_ == 0u) {
            return 0; // not armed
        }
        return val_ == 0u ? load_ : val_;
    }

    // P2: SysTick is the fast-forward source — the next COUNTFLAG reload is
    // exactly when a HAL_Delay poll loop would make progress.
    uint64_t cycles_until_next_event() const noexcept override {
        return cycles_until_next_tick();
    }

  private:
    uint32_t ctrl_ = 0;
    uint32_t load_ = 0;
    uint32_t val_ = 0;
    std::function<void()> irq_cb_;

    WeakPtrFactory<SysTickPeripheral> weak_factory_{this};
};

} // namespace micro_forge::periph
