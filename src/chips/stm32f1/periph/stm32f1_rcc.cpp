#include "chips/stm32f1/periph/stm32f1_rcc.hpp"

namespace micro_forge::chips::stm32f1 {

Expected<data_t> Stm32f1Rcc::read(addr_t offset, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }

    switch (offset) {
        case 0x00:
            return cr_;
        case 0x04:
            return cfgr_;
        case 0x08:
            return cir_;
        case 0x14:
            return ahbenr_;
        case 0x18:
            return apb2enr_;
        case 0x1C:
            return apb1enr_;
        default:
            return std::unexpected(BusError::PeripheralFault);
    }
}

Expected<void> Stm32f1Rcc::write(addr_t offset, data_t data, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }

    switch (offset) {
        case 0x00:
            apply_cr_write(data);
            return {};
        case 0x04: {
            cfgr_ = data;
            // SW write immediately reflected in SWS
            cfgr_ = (cfgr_ & ~(kSwMask << kSwsShift)) |
                    (((cfgr_ >> kSwShift) & kSwMask) << kSwsShift);
            return {};
        }
        case 0x08:
            cir_ = data;
            return {};
        case 0x14:
            ahbenr_ = data;
            return {};
        case 0x18:
            apb2enr_ = data;
            return {};
        case 0x1C:
            apb1enr_ = data;
            return {};
        default:
            return std::unexpected(BusError::PeripheralFault);
    }
}

bool Stm32f1Rcc::is_clock_enabled(uint32_t peripheral_addr) const {
    // APB2 peripherals
    if (peripheral_addr >= 0x40010000 && peripheral_addr < 0x40020000) {
        // GPIOA-C: bits 2-4, USART1: bit 14, TIM1: bit 11, ADC1: bit 9, AFIO:
        // bit 0
        if (peripheral_addr >= 0x40010800 && peripheral_addr < 0x40010C00) {
            return (apb2enr_ >> 2) & 1; // GPIOA
        }
        if (peripheral_addr >= 0x40010C00 && peripheral_addr < 0x40011000) {
            return (apb2enr_ >> 3) & 1; // GPIOB
        }
        if (peripheral_addr >= 0x40011000 && peripheral_addr < 0x40011400) {
            return (apb2enr_ >> 4) & 1; // GPIOC
        }
        if (peripheral_addr >= 0x40013800 && peripheral_addr < 0x40013C00) {
            return (apb2enr_ >> 14) & 1; // USART1
        }
    }
    // APB1 peripherals
    if (peripheral_addr >= 0x40000000 && peripheral_addr < 0x40010000) {
        if (peripheral_addr >= 0x40000000 && peripheral_addr < 0x40000400) {
            return (apb1enr_ >> 0) & 1; // TIM2
        }
    }
    return false;
}

void Stm32f1Rcc::enable_clock(uint32_t peripheral_addr) {
    if (peripheral_addr >= 0x40010800 && peripheral_addr < 0x40010C00) {
        apb2enr_ |= (1u << 2); // GPIOA
    } else if (peripheral_addr >= 0x40010C00 && peripheral_addr < 0x40011000) {
        apb2enr_ |= (1u << 3); // GPIOB
    } else if (peripheral_addr >= 0x40011000 && peripheral_addr < 0x40011400) {
        apb2enr_ |= (1u << 4); // GPIOC
    } else if (peripheral_addr >= 0x40013800 && peripheral_addr < 0x40013C00) {
        apb2enr_ |= (1u << 14); // USART1
    } else if (peripheral_addr >= 0x40000000 && peripheral_addr < 0x40000400) {
        apb1enr_ |= (1u << 0); // TIM2
    }
}

void Stm32f1Rcc::apply_cr_write(uint32_t data) {
    // Read-only bits: HSIRDY (1), HSERDY (17), PLLRDY (25)
    constexpr uint32_t kRoMask = kHsiRdy | kHseRdy | kPllRdy;
    cr_ = (data & ~kRoMask) | (cr_ & kRoMask);

    // HSION written → keep HSIRDY aligned
    if (cr_ & kHsiOn) {
        cr_ |= kHsiRdy;
    } else {
        cr_ &= ~kHsiRdy;
    }

    // HSEON written → HSERDY immediately ready
    if (cr_ & kHseOn) {
        cr_ |= kHseRdy;
    } else {
        cr_ &= ~kHseRdy;
    }

    // PLLON written → PLLRDY immediately ready
    if (cr_ & kPllOn) {
        cr_ |= kPllRdy;
    } else {
        cr_ &= ~kPllRdy;
    }
}

} // namespace micro_forge::chips::stm32f1
