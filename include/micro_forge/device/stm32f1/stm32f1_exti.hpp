#pragma once

#include "micro_forge/device/base/device.hpp"
#include "micro_forge/device/base/hooks/events.hpp"
#include "micro_forge/device/base/types.hpp"
#include "micro_forge/device/base/weak_ptr.hpp"

#include <cstdint>
#include <functional>

namespace micro_forge::device::stm32f1 {

class Stm32f1Afio;

// STM32F1 External Interrupt/Event controller (EXTI), mapped at 0x40010400.
// Each EXTI line 0..15 is routed to one GPIO port via AFIO EXTICR; a configured
// edge on that port's pin sets the pending bit and raises the line's NVIC IRQ.
/// @brief GPIO-edge and software-triggered external interrupt controller.
class Stm32f1Exti : public device::Device {
public:
    void reset() override;

    Stm32f1Exti() = default;

    // EXTI needs AFIO EXTICR to map a line number to the owning GPIO port.
    void set_afio(Stm32f1Afio& afio) { afio_ = &afio; }

    // Peripheral → CPU IRQ channel: invoked once per line that goes pending.
    void set_irq_callback(std::function<void(intr::intr_n_t)> cb) { irq_cb_ = std::move(cb); }

    // GPIO edge slot — subscribe this to each GPIO's edge_signal().
    void on_gpio_edge(const hooks::GpioEdge& edge);

    // Device
    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    [[nodiscard]] std::string_view name() const noexcept override { return "EXTI"; }

    [[nodiscard]] bool pending(uint8_t line) const { return ((pr_ >> line) & 1U) != 0U; }

    [[nodiscard]] WeakPtr<Stm32f1Exti> get_weak() { return weak_factory_.get_weak_ptr(); }

private:
    uint32_t imr_ = 0U;   // Interrupt mask (line enable)
    uint32_t emr_ = 0U;   // Event mask (stored; v1 has no event consumer)
    uint32_t rtsr_ = 0U;  // Rising-edge trigger select
    uint32_t ftsr_ = 0U;  // Falling-edge trigger select
    uint32_t swier_ = 0U; // Software interrupt trigger
    uint32_t pr_ = 0U;    // Pending (rc_w1: write 1 to clear)

    Stm32f1Afio* afio_ = nullptr;
    std::function<void(intr::intr_n_t)> irq_cb_;

    WeakPtrFactory<Stm32f1Exti> weak_factory_{this};

    void trigger_line(uint8_t line);
};

// EXTI line N → NVIC IRQ number (STM32F1 vector table). Lines 0-4 have
// individual IRQs (6-10); 5-9 share EXTI9_5 (23); 10-15 share EXTI15_10 (40).
inline intr::intr_n_t exti_irq_for_line(uint8_t line) {
    switch (line) {
    case 0U:
        return 6U;
    case 1U:
        return 7U;
    case 2U:
        return 8U;
    case 3U:
        return 9U;
    case 4U:
        return 10U;
    case 5U:
    case 6U:
    case 7U:
    case 8U:
    case 9U:
        return 23U;
    default:
        return 40U; // 10-15
    }
}

} // namespace micro_forge::device::stm32f1
