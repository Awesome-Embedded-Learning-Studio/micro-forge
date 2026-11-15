/**
 * @file uart.hpp
 * @brief Shared serial-byte engine behind every UART register map.
 * @version 0.1.0
 *
 * Owns the receive buffer, the ready state, the transmit sink and the
 * gated RX interrupt edge. Register layouts (16550 LSR/DLL, STM32
 * SR/CR1) stay with their devices; they translate registers into the
 * calls below instead of re-implementing the byte flow.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>

namespace micro_forge::device {

/** @brief Receive/transmit byte flow shared by all serial device models. */
class UartCore {
public:
    using OutputCallback = std::function<void(uint8_t)>;
    using IrqCallback = std::function<void()>;

    /**
     * @brief Builds the engine with a receive buffer bound.
     * @param[in] rx_capacity Maximum queued receive bytes; use
     *                        SIZE_MAX for the legacy unbounded queue.
     */
    explicit UartCore(std::size_t rx_capacity);

    /// @brief Clears the buffer and gating; output and IRQ callbacks stay wired.
    void reset();

    /// @brief Wires the transmit sink; without one, transmitted bytes drop.
    void set_output(OutputCallback callback);
    /// @brief Wires the device-to-interrupt-controller callback.
    void set_irq_callback(IrqCallback callback);

    /**
     * @brief Gates the RX interrupt; enabling while bytes are pending raises now.
     * @param[in] enabled The device's RX-interrupt enable register bit.
     */
    void set_rx_irq_enabled(bool enabled);

    /**
     * @brief Accepts one host-injected byte; raises the gated RX interrupt.
     * @param[in] byte Received byte; dropped silently when the buffer is full.
     */
    void inject_rx(uint8_t byte);

    /// @brief True while at least one received byte waits in the buffer.
    [[nodiscard]] bool rx_ready() const;

    /**
     * @brief Consumes the oldest received byte.
     * @return The byte; callers must check rx_ready() first.
     */
    [[nodiscard]] uint8_t read_rx();

    /**
     * @brief Hands one guest-transmitted byte to the wired output sink.
     * @param[in] byte Transmitted byte.
     */
    void transmit(uint8_t byte);

private:
    void raise_irq_if_pending();

    std::deque<uint8_t> rx_;
    std::size_t rx_capacity_;
    bool rx_irq_enabled_ = false;
    OutputCallback output_;
    IrqCallback irq_callback_;
};

} // namespace micro_forge::device
