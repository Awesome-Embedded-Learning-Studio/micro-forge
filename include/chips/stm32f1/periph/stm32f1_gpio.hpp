#pragma once

#include "hooks/events.hpp"
#include "hooks/signal.hpp"
#include "periph/device.hpp"
#include "periph/gpio.hpp"
#include "util/weak_ptr/weak_ptr_factory.hpp"

#include <cstdint>
#include <functional>

namespace micro_forge::chips::stm32f1 {

class Stm32f1Gpio : public periph::Device, public periph::Gpio {
  public:
    explicit Stm32f1Gpio(uint8_t port_id);

    // Device
    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    std::string_view name() const noexcept override;

    // Gpio
    void set_pin(uint8_t pin, bool high) override;
    bool get_pin(uint8_t pin) const override;
    void configure_pin(uint8_t pin, periph::PinMode mode,
                       periph::PinPull pull = periph::PinPull::None,
                       periph::PinSpeed speed = periph::PinSpeed::Low) override;
    void simulate_input(uint8_t pin, bool high) override;
    void set_pin_change_callback(PinChangeCallback cb) override;

    // ── Hook bus ──
    // Subscribers observe real output edges (ODR / BSRR / BRR / set_pin).
    hooks::Signal<hooks::GpioEdge>& edge_signal() { return edge_signal_; }
    // Output data register — backs the GPIO panel of the introspection
    // snapshot (low 16 bits = pin output levels).
    uint32_t odr() const noexcept { return odr_; }
    // Stamp emitted events with the simulator cycle; wire after the CPU is up.
    void set_cycle_source(std::function<uint64_t()> src) {
        cycle_source_ = std::move(src);
    }

    WeakPtr<Stm32f1Gpio> GetWeak() { return weak_factory_.GetWeakPtr(); }

  private:
    void on_odr_changed(uint32_t old_odr, uint32_t new_odr);

    uint32_t crl_ = 0x44444444;
    uint32_t crh_ = 0x44444444;
    uint32_t idr_ = 0;
    uint32_t odr_ = 0;
    uint32_t lckr_ = 0;
    uint8_t port_id_;
    PinChangeCallback on_pin_change_;
    hooks::Signal<hooks::GpioEdge> edge_signal_;
    std::function<uint64_t()> cycle_source_;

    WeakPtrFactory<Stm32f1Gpio> weak_factory_{this};
};

} // namespace micro_forge::chips::stm32f1
