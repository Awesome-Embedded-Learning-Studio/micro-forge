/**
 * @file uart.cpp
 * @brief Serial byte flow shared by all UART register maps.
 * @version 0.1.0
 */
#include "micro_forge/device/base/uart.hpp"

#include <utility>

namespace micro_forge::device {

UartCore::UartCore(std::size_t rx_capacity) : rx_capacity_(rx_capacity) {}

void UartCore::reset() {
    rx_.clear();
    rx_irq_enabled_ = false;
}

void UartCore::set_output(OutputCallback callback) {
    output_ = std::move(callback);
}

void UartCore::set_irq_callback(IrqCallback callback) {
    irq_callback_ = std::move(callback);
}

void UartCore::raise_irq_if_pending() {
    if (rx_irq_enabled_ && !rx_.empty() && irq_callback_) {
        irq_callback_();
    }
}

void UartCore::set_rx_irq_enabled(bool enabled) {
    rx_irq_enabled_ = enabled;
    raise_irq_if_pending();
}

void UartCore::inject_rx(uint8_t byte) {
    if (rx_.size() < rx_capacity_) {
        rx_.push_back(byte);
        raise_irq_if_pending();
    }
}

bool UartCore::rx_ready() const {
    return !rx_.empty();
}

uint8_t UartCore::read_rx() {
    const auto byte = rx_.front();
    rx_.pop_front();
    return byte;
}

void UartCore::transmit(uint8_t byte) {
    if (output_) {
        output_(byte);
    }
}

} // namespace micro_forge::device
