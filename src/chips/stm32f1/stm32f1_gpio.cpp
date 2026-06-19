#include "chips/stm32f1/stm32f1_gpio.hpp"
#include "util/logger.hpp"

namespace micro_forge::chips::stm32f1 {

Stm32f1Gpio::Stm32f1Gpio(uint8_t port_id) : port_id_(port_id) {}

Expected<data_t> Stm32f1Gpio::read(addr_t offset, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }

    switch (offset) {
        case 0x00:
            return crl_;
        case 0x04:
            return crh_;
        case 0x08:
            return idr_;
        case 0x0C:
            return odr_;
        case 0x18:
            return lckr_;
        default:
            return std::unexpected(BusError::PeripheralFault);
    }
}

Expected<void> Stm32f1Gpio::write(addr_t offset, data_t data, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }

    switch (offset) {
        case 0x00:
            crl_ = data;
            return {};
        case 0x04:
            crh_ = data;
            return {};
        case 0x08:
            return std::unexpected(BusError::ReadOnly); // IDR read-only
        case 0x0C: {
            uint32_t old = odr_;
            odr_ = data;
            on_odr_changed(old, odr_);
            return {};
        }
        case 0x10: { // BSRR — write-only set/reset
            uint32_t old = odr_;
            uint32_t set_bits = data & 0xFFFF;
            uint32_t reset_bits = (data >> 16) & 0xFFFF;
            odr_ = (odr_ | set_bits) & ~reset_bits;
            on_odr_changed(old, odr_);
            return {};
        }
        case 0x14: { // BRR — write-only reset
            uint32_t old = odr_;
            odr_ &= ~(data & 0xFFFF);
            on_odr_changed(old, odr_);
            return {};
        }
        case 0x18:
            lckr_ = data;
            return {};
        default:
            return std::unexpected(BusError::PeripheralFault);
    }
}

void Stm32f1Gpio::set_pin(uint8_t pin, bool high) {
    if (pin > 15) {
        return;
    }
    uint32_t old = odr_;
    if (high) {
        odr_ |= (1u << pin);
    } else {
        odr_ &= ~(1u << pin);
    }
    on_odr_changed(old, odr_);
}

bool Stm32f1Gpio::get_pin(uint8_t pin) const {
    if (pin > 15) {
        return false;
    }
    return (odr_ >> pin) & 1;
}

void Stm32f1Gpio::configure_pin(uint8_t pin, periph::PinMode mode,
                                periph::PinPull /*pull*/,
                                periph::PinSpeed speed) {
    if (pin > 15) {
        return;
    }

    // Each pin occupies 4 bits in CRL (pin 0-7) or CRH (pin 8-15)
    uint32_t& reg = (pin < 8) ? crl_ : crh_;
    uint8_t bit_pos = (pin % 8) * 4;

    uint8_t cfg = 0;
    switch (mode) {
        case periph::PinMode::Input:
            cfg = 0x0; // Input floating
            break;
        case periph::PinMode::Output:
            switch (speed) {
                case periph::PinSpeed::Low:
                    cfg = 0x2;
                    break; // Output push-pull, 2MHz
                case periph::PinSpeed::Medium:
                    cfg = 0x5;
                    break; // Output push-pull, 10MHz
                case periph::PinSpeed::High:
                    cfg = 0x3;
                    break; // Output push-pull, 50MHz
            }
            break;
        case periph::PinMode::Alternate:
            cfg = 0x8;
            break; // AF push-pull
        case periph::PinMode::Analog:
            cfg = 0x0;
            break; // Analog same as input
    }

    reg = (reg & ~(0xFu << bit_pos)) | (static_cast<uint32_t>(cfg) << bit_pos);
}

void Stm32f1Gpio::simulate_input(uint8_t pin, bool high) {
    if (pin > 15) {
        return;
    }
    if (high) {
        idr_ |= (1u << pin);
    } else {
        idr_ &= ~(1u << pin);
    }
}

void Stm32f1Gpio::set_pin_change_callback(PinChangeCallback cb) {
    on_pin_change_ = std::move(cb);
}

void Stm32f1Gpio::on_odr_changed(uint32_t old_odr, uint32_t new_odr) {
    uint32_t changed = old_odr ^ new_odr;
    if (!changed) {
        return;
    }

    for (uint8_t i = 0; i < 16; ++i) {
        if ((changed >> i) & 1) {
            bool high = (new_odr >> i) & 1;
            LOG_DEBUG("gpio", "GPIO%c.PIN%u -> %s", port_id_, i,
                      high ? "HIGH" : "LOW");
            if (on_pin_change_) {
                on_pin_change_(i, high);
            }
            if (!edge_signal_.empty()) {
                edge_signal_.emit(hooks::GpioEdge{
                    {cycle_source_ ? cycle_source_() : 0},
                    static_cast<char>(port_id_), i, high});
            }
        }
    }
}

std::string_view Stm32f1Gpio::name() const noexcept {
    static constexpr std::string_view names[] = {"GPIOA", "GPIOB", "GPIOC",
                                                 "GPIOD", "GPIOE"};
    if (port_id_ < 'A' || port_id_ > 'E') {
        return "GPIO?";
    }
    auto idx = static_cast<size_t>(port_id_ - 'A');
    return names[idx];
}

} // namespace micro_forge::chips::stm32f1
