#pragma once

#include "micro_forge/device/base/device.hpp"
#include "micro_forge/device/base/serial_port.hpp"
#include "micro_forge/device/base/uart.hpp"
#include "micro_forge/device/base/weak_ptr.hpp"

#include <cstdint>
#include <functional>

namespace micro_forge::device::stm32f1 {

/// @brief STM32F1 serial transmit, injected receive and IRQ register model.
class Stm32f1Usart : public device::Device, public device::SerialPort {
public:
    void reset() override;

    Stm32f1Usart() = default;

    // Device
    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    [[nodiscard]] std::string_view name() const noexcept override { return "USART"; }

    // SerialPort
    void send_byte(uint8_t byte) override;
    [[nodiscard]] bool can_send() const override;
    void set_output(OutputCallback cb) override;

    // RX injection (host → firmware): buffers a received byte, sets RXNE, and
    // raises the USART IRQ if RXNEIE (CR1 bit5) is enabled.
    void inject_rx(uint8_t byte);

    // Peripheral → CPU IRQ channel (machine wires this to raise the USART IRQ).
    void set_irq_callback(std::function<void()> cb) { core_.set_irq_callback(std::move(cb)); }

    [[nodiscard]] WeakPtr<Stm32f1Usart> get_weak() { return weak_factory_.get_weak_ptr(); }

private:
    uint32_t sr_ = 0x000000C0U;
    uint32_t dr_ = 0U;
    uint32_t brr_ = 0U;
    uint32_t cr1_ = 0U;
    uint32_t cr2_ = 0U;
    uint32_t cr3_ = 0U;

    // Single-slot receive buffer, transmit sink and gated RX interrupt.
    UartCore core_{1};

    WeakPtrFactory<Stm32f1Usart> weak_factory_{this};
};

} // namespace micro_forge::device::stm32f1
