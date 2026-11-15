#pragma once

#include "micro_forge/device/base/device.hpp"
#include "micro_forge/device/base/timer.hpp"
#include "micro_forge/device/base/weak_ptr.hpp"

#include <cstdint>
#include <functional>

namespace micro_forge::device::stm32f1 {

/// @brief 16-bit internal-clock timer with stored HAL configuration registers.
class Stm32f1Timer : public device::Device, public device::Timer {
public:
    void reset() override;

    Stm32f1Timer() = default;

    // Device
    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    void tick(uint64_t cycles) override;
    [[nodiscard]] std::string_view name() const noexcept override { return "TIM"; }

    // Timer
    void enable(bool en) override;
    void set_prescaler(uint32_t psc) override;
    void set_auto_reload(uint32_t arr) override;
    [[nodiscard]] uint32_t counter() const override;
    [[nodiscard]] bool update_flag() const override;
    void clear_update_flag() override;

    // Peripheral → CPU IRQ channel: invoked on UIF 0→1 edge when DIER.UIE is
    // set.
    void set_irq_callback(std::function<void()> cb) { irq_cb_ = std::move(cb); }

    [[nodiscard]] WeakPtr<Stm32f1Timer> get_weak() { return weak_factory_.get_weak_ptr(); }

private:
    void generate_update();

    uint32_t cr1_ = 0U;
    // Configuration storage for HAL initialization. External slave/trigger
    // timing selected by CR2/SMCR is not modeled.
    uint32_t cr2_ = 0U;
    uint32_t smcr_ = 0U;
    uint32_t dier_ = 0U;
    uint32_t sr_ = 0U;
    uint32_t psc_ = 0U;
    uint32_t arr_ = 0xFFFFU;
    uint32_t cnt_ = 0U;
    uint64_t prescaler_residual_ = 0U;

    std::function<void()> irq_cb_;

    WeakPtrFactory<Stm32f1Timer> weak_factory_{this};
};

} // namespace micro_forge::device::stm32f1
