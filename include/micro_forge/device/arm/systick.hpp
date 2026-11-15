#pragma once

#include "micro_forge/device/base/countdown.hpp"
#include "micro_forge/device/base/device.hpp"
#include "micro_forge/device/base/weak_ptr.hpp"

#include <cstdint>
#include <functional>

namespace micro_forge::device::arm {

/// @brief 24-bit SysTick driven in input-clock ticks by the machine.
class SysTickPeripheral : public Device {
public:
    void reset() override;

    SysTickPeripheral() : core_{CountdownCore::Config{.max_reload = 0x00FFFFFFU}} {
        core_.set_expire_callback([this] {
            if (((ctrl_ & 0x2U) != 0U) && irq_cb_) {
                irq_cb_();
            }
        });
    }

    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    void tick(uint64_t cycles) override;
    [[nodiscard]] std::string_view name() const noexcept override { return "SysTick"; }

    [[nodiscard]] WeakPtr<SysTickPeripheral> get_weak() { return weak_factory_.get_weak_ptr(); }

    void set_irq_callback(std::function<void()> cb) { irq_cb_ = std::move(cb); }

    // Read-only accessors backing the SysTick panel of the introspection
    // snapshot (milestone 04 GUI). COUNTFLAG lives in ctrl bit 16.
    [[nodiscard]] uint32_t ctrl() const noexcept { return ctrl_; }
    [[nodiscard]] uint32_t load() const noexcept { return core_.reload(); }
    [[nodiscard]] uint32_t val() const noexcept { return core_.value(); }

    // SysTick input-clock cycles until the next transition to zero, or 0
    // when disabled or idle with LOAD=0. The machine applies CLKSOURCE
    // division before tick(); a zero current value needs one reload cycle.
    // This query has no side effects.
    [[nodiscard]] uint64_t cycles_until_next_tick() const noexcept {
        if ((ctrl_ & 0x1U) == 0U) {
            return 0U;
        }
        if (core_.value() != 0U) {
            return core_.value();
        }
        return core_.reload() == 0U ? 0U : static_cast<uint64_t>(core_.reload()) + 1U;
    }

    // Fast-forward queries use the same input-clock units as tick().
    [[nodiscard]] uint64_t cycles_until_next_event() const noexcept override {
        return cycles_until_next_tick();
    }

private:
    uint32_t ctrl_ = 0U;
    CountdownCore core_;
    std::function<void()> irq_cb_;

    WeakPtrFactory<SysTickPeripheral> weak_factory_{this};
};

} // namespace micro_forge::device::arm
