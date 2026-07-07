#include "chips/stm32f1/periph/stm32f1_exti.hpp"
#include "chips/stm32f1/periph/stm32f1_afio.hpp"

namespace micro_forge::chips::stm32f1 {

Expected<data_t> Stm32f1Exti::read(addr_t offset, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }
    switch (offset) {
        case 0x00:
            return imr_;
        case 0x04:
            return emr_;
        case 0x08:
            return rtsr_;
        case 0x0C:
            return ftsr_;
        case 0x10:
            return swier_;
        case 0x14:
            return pr_;
        default:
            return std::unexpected(BusError::PeripheralFault);
    }
}

Expected<void> Stm32f1Exti::write(addr_t offset, data_t data, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }
    switch (offset) {
        case 0x00:
            imr_ = data;
            return {};
        case 0x04:
            emr_ = data;
            return {};
        case 0x08:
            rtsr_ = data;
            return {};
        case 0x0C:
            ftsr_ = data;
            return {};
        case 0x10:
            // SWIER: writing 1 software-triggers a line if IMR is enabled and
            // it is not already pending. Bits without IMR are ignored.
            swier_ = data;
            for (uint8_t line = 0; line < 16; ++line) {
                if ((data & (1u << line)) && (imr_ & (1u << line)) &&
                    !(pr_ & (1u << line))) {
                    trigger_line(line);
                }
            }
            return {};
        case 0x14:
            // PR is rc_w1: writing 1 clears the pending bit.
            pr_ &= ~data;
            return {};
        default:
            return std::unexpected(BusError::PeripheralFault);
    }
}

void Stm32f1Exti::on_gpio_edge(const hooks::GpioEdge& edge) {
    uint8_t line = edge.pin;
    if (line > 15) {
        return;
    }
    if (!(imr_ & (1u << line))) {
        return; // line not unmasked
    }
    // EXTICR routes this line to exactly one GPIO port; only an edge on that
    // port fires.
    if (afio_) {
        uint8_t port_sel = afio_->exti_line_port(line); // 0=PA,1=PB,...
        char expected = static_cast<char>('A' + port_sel);
        if (edge.port != expected) {
            return;
        }
    }
    bool match = (edge.rising && (rtsr_ & (1u << line))) ||
                 (!edge.rising && (ftsr_ & (1u << line)));
    if (match) {
        trigger_line(line);
    }
}

void Stm32f1Exti::trigger_line(uint8_t line) {
    pr_ |= (1u << line);
    if (irq_cb_) {
        irq_cb_(exti_irq_for_line(line));
    }
}

} // namespace micro_forge::chips::stm32f1
