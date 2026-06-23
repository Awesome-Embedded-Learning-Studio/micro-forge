#pragma once

#include "periph/device.hpp"
#include "periph/timer.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"

#include <cstdint>
#include <functional>

namespace micro_forge::chips::stm32f1 {

class Stm32f1Timer : public periph::Device, public periph::Timer {
  public:
    Stm32f1Timer() = default;

    // Device
    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    void tick(uint64_t cycles) override;
    std::string_view name() const noexcept override { return "TIM"; }

    // Timer
    void enable(bool en) override;
    void set_prescaler(uint32_t psc) override;
    void set_auto_reload(uint32_t arr) override;
    uint32_t counter() const override;
    bool update_flag() const override;
    void clear_update_flag() override;

    // Peripheral → CPU IRQ channel: invoked on UIF 0→1 edge when DIER.UIE is set.
    void set_irq_callback(std::function<void()> cb) { irq_cb_ = std::move(cb); }

    WeakPtr<Stm32f1Timer> GetWeak() { return weak_factory_.GetWeakPtr(); }

  private:
    uint32_t cr1_ = 0;
    uint32_t dier_ = 0;
    uint32_t sr_ = 0;
    uint32_t psc_ = 0;
    uint32_t arr_ = 0;
    uint32_t cnt_ = 0;
    uint64_t prescaler_residual_ = 0;

    std::function<void()> irq_cb_;

    WeakPtrFactory<Stm32f1Timer> weak_factory_{this};
};

} // namespace micro_forge::chips::stm32f1
