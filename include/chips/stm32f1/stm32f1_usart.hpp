#pragma once

#include "periph/device.hpp"
#include "periph/serial_port.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"

#include <cstdint>
#include <functional>

namespace micro_forge::chips::stm32f1 {

class Stm32f1Usart : public periph::Device, public periph::SerialPort {
  public:
    Stm32f1Usart() = default;

    // Device
    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    std::string_view name() const noexcept override { return "USART"; }

    // SerialPort
    void send_byte(uint8_t byte) override;
    bool can_send() const override;
    void set_output(OutputCallback cb) override;

    // RX injection (host → firmware): buffers a received byte, sets RXNE, and
    // raises the USART IRQ if RXNEIE (CR1 bit5) is enabled.
    void inject_rx(uint8_t byte);

    // Peripheral → CPU IRQ channel (SoC wires this to raise the USART IRQ).
    void set_irq_callback(std::function<void()> cb) { irq_cb_ = std::move(cb); }

    WeakPtr<Stm32f1Usart> GetWeak() { return weak_factory_.GetWeakPtr(); }

  private:
    uint32_t sr_ = 0x000000C0;
    uint32_t dr_ = 0;
    uint32_t brr_ = 0;
    uint32_t cr1_ = 0;
    uint32_t cr2_ = 0;
    uint32_t cr3_ = 0;

    uint8_t rx_dr_ = 0; // received byte (single-slot buffer)

    OutputCallback output_;
    std::function<void()> irq_cb_;

    WeakPtrFactory<Stm32f1Usart> weak_factory_{this};
};

} // namespace micro_forge::chips::stm32f1
