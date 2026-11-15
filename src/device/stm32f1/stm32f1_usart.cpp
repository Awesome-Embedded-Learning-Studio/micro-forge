#include "micro_forge/device/stm32f1/stm32f1_usart.hpp"
#include "micro_forge/device/base/types.hpp"

#include <cstdint>
#include <cstdio>
#include <expected>
#include <utility>

namespace micro_forge::device::stm32f1 {

void Stm32f1Usart::reset() {
    sr_ = 0x000000C0U;
    dr_ = 0U;
    brr_ = 0U;
    cr1_ = 0U;
    cr2_ = 0U;
    cr3_ = 0U;
    core_.reset();
}

Expected<data_t> Stm32f1Usart::read(addr_t offset, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }

    switch (offset) {
    case 0x00U:
        return sr_;
    case 0x04U:
        // DR read returns the received byte and clears RXNE.
        sr_ &= ~(1U << 5U);
        return core_.rx_ready() ? core_.read_rx() : 0U;
    case 0x08U:
        return brr_;
    case 0x0CU:
        return cr1_;
    case 0x10U:
        return cr2_;
    case 0x14U:
        return cr3_;
    default:
        return std::unexpected(BusError::kPeripheralFault);
    }
}

Expected<void> Stm32f1Usart::write(addr_t offset, data_t data, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }

    switch (offset) {
    case 0x00U:
        sr_ = data;
        return {};
    case 0x04U: {
        dr_ = data;
        core_.transmit(static_cast<uint8_t>(data & 0xFFU));
        return {};
    }
    case 0x08U:
        brr_ = data;
        return {};
    case 0x0CU:
        cr1_ = data;
        // Enabling RXNEIE while RXNE is already pending must raise now.
        core_.set_rx_irq_enabled((cr1_ & (1U << 5U)) != 0U);
        return {};
    case 0x10U:
        cr2_ = data;
        return {};
    case 0x14U:
        cr3_ = data;
        return {};
    default:
        return std::unexpected(BusError::kPeripheralFault);
    }
}

void Stm32f1Usart::send_byte(uint8_t byte) {
    dr_ = byte;
    core_.transmit(byte);
}

bool Stm32f1Usart::can_send() const {
    return (sr_ & 0x80U) != 0U; // TXE always 1
}

void Stm32f1Usart::set_output(OutputCallback cb) {
    core_.set_output(std::move(cb));
}

void Stm32f1Usart::inject_rx(uint8_t byte) {
    core_.inject_rx(byte); // Raises the gated RX interrupt while RXNEIE is set.
    sr_ |= (1U << 5U);     // RXNE
}

} // namespace micro_forge::device::stm32f1
