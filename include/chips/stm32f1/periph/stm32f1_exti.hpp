#pragma once

#include "cpu/intr.hpp"
#include "hooks/events.hpp"
#include "periph/device.hpp"
#include "util/weak_ptr/weak_ptr_factory.hpp"

#include <cstdint>
#include <functional>

namespace micro_forge::chips::stm32f1 {

class Stm32f1Afio;

// STM32F1 External Interrupt/Event controller (EXTI), mapped at 0x40010400.
// Each EXTI line 0..15 is routed to one GPIO port via AFIO EXTICR; a configured
// edge on that port's pin sets the pending bit and raises the line's NVIC IRQ.
class Stm32f1Exti : public periph::Device {
  public:
    Stm32f1Exti() = default;

    // EXTI needs AFIO EXTICR to map a line number to the owning GPIO port.
    void set_afio(Stm32f1Afio& afio) { afio_ = &afio; }

    // Peripheral → CPU IRQ channel: invoked once per line that goes pending.
    void set_irq_callback(std::function<void(intr::intr_n_t)> cb) {
        irq_cb_ = std::move(cb);
    }

    // GPIO edge slot — subscribe this to each GPIO's edge_signal().
    void on_gpio_edge(const hooks::GpioEdge& edge);

    // Device
    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    std::string_view name() const noexcept override { return "EXTI"; }

    bool pending(uint8_t line) const { return (pr_ >> line) & 1u; }

    WeakPtr<Stm32f1Exti> GetWeak() { return weak_factory_.GetWeakPtr(); }

  private:
    uint32_t imr_ = 0;   // Interrupt mask (line enable)
    uint32_t emr_ = 0;   // Event mask (stored; v1 has no event consumer)
    uint32_t rtsr_ = 0;  // Rising-edge trigger select
    uint32_t ftsr_ = 0;  // Falling-edge trigger select
    uint32_t swier_ = 0; // Software interrupt trigger
    uint32_t pr_ = 0;    // Pending (rc_w1: write 1 to clear)

    Stm32f1Afio* afio_ = nullptr;
    std::function<void(intr::intr_n_t)> irq_cb_;

    WeakPtrFactory<Stm32f1Exti> weak_factory_{this};

    void trigger_line(uint8_t line);
};

// EXTI line N → NVIC IRQ number (STM32F1 vector table). Lines 0-4 have
// individual IRQs (6-10); 5-9 share EXTI9_5 (23); 10-15 share EXTI15_10 (40).
inline intr::intr_n_t exti_irq_for_line(uint8_t line) {
    switch (line) {
        case 0:
            return 6;
        case 1:
            return 7;
        case 2:
            return 8;
        case 3:
            return 9;
        case 4:
            return 10;
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
            return 23;
        default:
            return 40; // 10-15
    }
}

} // namespace micro_forge::chips::stm32f1
