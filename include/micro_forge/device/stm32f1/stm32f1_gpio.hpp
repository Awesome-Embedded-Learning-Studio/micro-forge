#pragma once

#include "micro_forge/device/base/device.hpp"
#include "micro_forge/device/base/gpio.hpp"
#include "micro_forge/device/base/hooks/events.hpp"
#include "micro_forge/device/base/hooks/signal.hpp"
#include "micro_forge/device/base/weak_ptr.hpp"

#include <cstdint>
#include <functional>

namespace micro_forge::device::stm32f1 {

/// @brief STM32F1 GPIO registers with pin callbacks and edge events.
class Stm32f1Gpio : public device::Device, public device::Gpio {
public:
    void reset() override;

    explicit Stm32f1Gpio(uint8_t port_id);

    // Device
    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    [[nodiscard]] std::string_view name() const noexcept override;

    // Gpio
    void set_pin(uint8_t pin, bool high) override;
    [[nodiscard]] bool get_pin(uint8_t pin) const override;
    void configure_pin(uint8_t pin, device::PinMode mode,
                       device::PinPull pull = device::PinPull::kNone,
                       device::PinSpeed speed = device::PinSpeed::kLow) override;
    void simulate_input(uint8_t pin, bool high) override;
    void set_pin_change_callback(PinChangeCallback cb) override;

    // ── Hook bus ──
    // Subscribers observe real output edges (ODR / BSRR / BRR / set_pin).
    hooks::Signal<hooks::GpioEdge>& edge_signal() { return edge_signal_; }
    // Output data register — backs the GPIO panel of the introspection
    // snapshot (low 16 bits = pin output levels).
    [[nodiscard]] uint32_t odr() const noexcept { return odr_; }
    // Stamp emitted events with the simulator cycle; wire after the CPU is up.
    void set_cycle_source(std::function<uint64_t()> src) { cycle_source_ = std::move(src); }

    [[nodiscard]] WeakPtr<Stm32f1Gpio> get_weak() { return weak_factory_.get_weak_ptr(); }

private:
    void on_odr_changed(uint32_t old_odr, uint32_t new_odr);

    uint32_t crl_ = 0x44444444U;
    uint32_t crh_ = 0x44444444U;
    uint32_t idr_ = 0U;
    uint32_t odr_ = 0U;
    uint32_t lckr_ = 0U;
    uint8_t port_id_;
    PinChangeCallback on_pin_change_;
    hooks::Signal<hooks::GpioEdge> edge_signal_;
    std::function<uint64_t()> cycle_source_;

    WeakPtrFactory<Stm32f1Gpio> weak_factory_{this};
};

} // namespace micro_forge::device::stm32f1
