#include "micro_forge/device/stm32f1/stm32f1_exti.hpp"
#include "micro_forge/device/base/hooks/events.hpp"
#include "micro_forge/device/base/types.hpp"
#include "micro_forge/device/stm32f1/stm32f1_afio.hpp"
#include <cstdint>
#include <expected>

namespace micro_forge::device::stm32f1 {

void Stm32f1Exti::reset() {
    imr_ = 0U;
    emr_ = 0U;
    rtsr_ = 0U;
    ftsr_ = 0U;
    swier_ = 0U;
    pr_ = 0U;
}

Expected<data_t> Stm32f1Exti::read(addr_t offset, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }
    switch (offset) {
    case 0x00U:
        return imr_;
    case 0x04U:
        return emr_;
    case 0x08U:
        return rtsr_;
    case 0x0CU:
        return ftsr_;
    case 0x10U:
        return swier_;
    case 0x14U:
        return pr_;
    default:
        return std::unexpected(BusError::kPeripheralFault);
    }
}

Expected<void> Stm32f1Exti::write(addr_t offset, data_t data, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }
    switch (offset) {
    case 0x00U:
        imr_ = data;
        return {};
    case 0x04U:
        emr_ = data;
        return {};
    case 0x08U:
        rtsr_ = data;
        return {};
    case 0x0CU:
        ftsr_ = data;
        return {};
    case 0x10U:
        // SWIER: writing 1 software-triggers a line if IMR is enabled and
        // it is not already pending. Bits without IMR are ignored.
        swier_ = data;
        for (uint8_t line = 0U; line < 16U; ++line) {
            if (((data & (1U << line)) != 0U) && ((imr_ & (1U << line)) != 0U) &&
                ((pr_ & (1U << line)) == 0U)) {
                trigger_line(line);
            }
        }
        return {};
    case 0x14U:
        // PR is rc_w1: writing 1 clears the pending bit.
        pr_ &= ~data;
        return {};
    default:
        return std::unexpected(BusError::kPeripheralFault);
    }
}

void Stm32f1Exti::on_gpio_edge(const hooks::GpioEdge& edge) {
    uint8_t const line = edge.pin;
    if (line > 15U) {
        return;
    }
    if ((imr_ & (1U << line)) == 0U) {
        return; // line not unmasked
    }
    // EXTICR routes this line to exactly one GPIO port; only an edge on that
    // port fires.
    if (afio_ != nullptr) {
        uint8_t const port_sel = afio_->exti_line_port(line); // 0=PA,1=PB,...
        char const expected = static_cast<char>('A' + port_sel);
        if (edge.port != expected) {
            return;
        }
    }
    bool const match = (edge.rising && ((rtsr_ & (1U << line)) != 0U)) ||
                       (!edge.rising && ((ftsr_ & (1U << line)) != 0U));
    if (match) {
        trigger_line(line);
    }
}

void Stm32f1Exti::trigger_line(uint8_t line) {
    pr_ |= (1U << line);
    if (irq_cb_) {
        irq_cb_(exti_irq_for_line(line));
    }
}

} // namespace micro_forge::device::stm32f1
