#include "micro_forge/device/stm32f1/stm32f1_gpio.hpp"
#include "micro_forge/device/base/gpio.hpp"
#include "micro_forge/device/base/hooks/events.hpp"
#include "micro_forge/device/base/types.hpp"
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <utility>

namespace micro_forge::device::stm32f1 {

void Stm32f1Gpio::reset() {
    crl_ = 0x44444444U;
    crh_ = 0x44444444U;
    idr_ = 0U;
    odr_ = 0U;
    lckr_ = 0U;
}

Stm32f1Gpio::Stm32f1Gpio(uint8_t port_id) : port_id_(port_id) {}

Expected<data_t> Stm32f1Gpio::read(addr_t offset, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }

    switch (offset) {
    case 0x00U:
        return crl_;
    case 0x04U:
        return crh_;
    case 0x08U:
        return idr_;
    case 0x0CU:
        return odr_;
    case 0x18U:
        return lckr_;
    default:
        return std::unexpected(BusError::kPeripheralFault);
    }
}

Expected<void> Stm32f1Gpio::write(addr_t offset, data_t data, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }

    switch (offset) {
    case 0x00U:
        crl_ = data;
        return {};
    case 0x04U:
        crh_ = data;
        return {};
    case 0x08U:
        return std::unexpected(BusError::kReadOnly); // IDR read-only
    case 0x0CU: {
        uint32_t const old = odr_;
        odr_ = data;
        on_odr_changed(old, odr_);
        return {};
    }
    case 0x10U: { // BSRR — write-only set/reset
        uint32_t const old = odr_;
        uint32_t const set_bits = data & 0xFFFFU;
        uint32_t const reset_bits = (data >> 16U) & 0xFFFFU;
        odr_ = (odr_ | set_bits) & ~reset_bits;
        on_odr_changed(old, odr_);
        return {};
    }
    case 0x14U: { // BRR — write-only reset
        uint32_t const old = odr_;
        odr_ &= ~(data & 0xFFFFU);
        on_odr_changed(old, odr_);
        return {};
    }
    case 0x18U:
        lckr_ = data;
        return {};
    default:
        return std::unexpected(BusError::kPeripheralFault);
    }
}

void Stm32f1Gpio::set_pin(uint8_t pin, bool high) {
    if (pin > 15U) {
        return;
    }
    uint32_t const old = odr_;
    if (high) {
        odr_ |= (1U << pin);
    } else {
        odr_ &= ~(1U << pin);
    }
    on_odr_changed(old, odr_);
}

bool Stm32f1Gpio::get_pin(uint8_t pin) const {
    if (pin > 15U) {
        return false;
    }
    return ((odr_ >> pin) & 1U) != 0U;
}

void Stm32f1Gpio::configure_pin(uint8_t pin, device::PinMode mode, device::PinPull /*pull*/,
                                device::PinSpeed speed) {
    if (pin > 15U) {
        return;
    }

    // Each pin occupies 4 bits in CRL (pin 0-7) or CRH (pin 8-15)
    uint32_t& reg = (pin < 8U) ? crl_ : crh_;
    uint8_t const bit_pos = (pin % 8U) * 4U;

    uint8_t cfg = 0U;
    switch (mode) {
    case device::PinMode::kInput:
        cfg = 0x0U; // Input floating
        break;
    case device::PinMode::kOutput:
        switch (speed) {
        case device::PinSpeed::kLow:
            cfg = 0x2U;
            break; // Output push-pull, 2MHz
        case device::PinSpeed::kMedium:
            cfg = 0x5U;
            break; // Output push-pull, 10MHz
        case device::PinSpeed::kHigh:
            cfg = 0x3U;
            break; // Output push-pull, 50MHz
        }
        break;
    case device::PinMode::kAlternate:
        cfg = 0x8U;
        break; // AF push-pull
    case device::PinMode::kAnalog:
        cfg = 0x0U;
        break; // Analog same as input
    }

    reg = (reg & ~(0xFU << bit_pos)) | (static_cast<uint32_t>(cfg) << bit_pos);
}

void Stm32f1Gpio::simulate_input(uint8_t pin, bool high) {
    if (pin > 15U) {
        return;
    }
    bool const was_high = ((idr_ >> pin) & 1U) != 0U;
    if (high) {
        idr_ |= (1U << pin);
    } else {
        idr_ &= ~(1U << pin);
    }
    // An external input edge feeds EXTI the same way an output edge does.
    bool const now_high = ((idr_ >> pin) & 1U) != 0U;
    if (was_high != now_high && !edge_signal_.empty()) {
        edge_signal_.emit(hooks::GpioEdge{
            {cycle_source_ ? cycle_source_() : 0U},
            static_cast<char>(port_id_),
            pin,
            now_high,
        });
    }
}

void Stm32f1Gpio::set_pin_change_callback(PinChangeCallback cb) {
    on_pin_change_ = std::move(cb);
}

void Stm32f1Gpio::on_odr_changed(uint32_t old_odr, uint32_t new_odr) {
    uint32_t const changed = old_odr ^ new_odr;
    if (changed == 0U) {
        return;
    }

    for (uint8_t i = 0U; i < 16U; ++i) {
        if (((changed >> i) & 1U) != 0U) {
            bool const high = ((new_odr >> i) & 1U) != 0U;
            if (on_pin_change_) {
                on_pin_change_(i, high);
            }
            if (!edge_signal_.empty()) {
                edge_signal_.emit(hooks::GpioEdge{
                    {cycle_source_ ? cycle_source_() : 0U},
                    static_cast<char>(port_id_),
                    i,
                    high,
                });
            }
        }
    }
}

std::string_view Stm32f1Gpio::name() const noexcept {
    static constexpr std::string_view names[] = {
        "GPIOA", "GPIOB", "GPIOC", "GPIOD", "GPIOE",
    };
    if (port_id_ < 'A' || port_id_ > 'E') {
        return "GPIO?";
    }
    auto const idx = static_cast<size_t>(port_id_ - 'A');
    return names[idx];
}

} // namespace micro_forge::device::stm32f1
