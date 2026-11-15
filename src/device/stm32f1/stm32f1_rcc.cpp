#include "micro_forge/device/stm32f1/stm32f1_rcc.hpp"
#include "micro_forge/device/base/types.hpp"
#include <cstdint>
#include <expected>

namespace micro_forge::device::stm32f1 {

void Stm32f1Rcc::reset() {
    cr_ = 0x00000083U;
    cfgr_ = 0U;
    cir_ = 0U;
    ahbenr_ = 0U;
    apb2enr_ = 0U;
    apb1enr_ = 0U;
}

Expected<data_t> Stm32f1Rcc::read(addr_t offset, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }

    switch (offset) {
    case 0x00U:
        return cr_;
    case 0x04U:
        return cfgr_;
    case 0x08U:
        return cir_;
    case 0x14U:
        return ahbenr_;
    case 0x18U:
        return apb2enr_;
    case 0x1CU:
        return apb1enr_;
    default:
        return std::unexpected(BusError::kPeripheralFault);
    }
}

Expected<void> Stm32f1Rcc::write(addr_t offset, data_t data, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }

    switch (offset) {
    case 0x00U:
        apply_cr_write(data);
        return {};
    case 0x04U: {
        cfgr_ = data;
        // SW write immediately reflected in SWS
        cfgr_ = (cfgr_ & ~(kSwMask << kSwsShift)) | (((cfgr_ >> kSwShift) & kSwMask) << kSwsShift);
        return {};
    }
    case 0x08U:
        cir_ = data;
        return {};
    case 0x14U:
        ahbenr_ = data;
        return {};
    case 0x18U:
        apb2enr_ = data;
        return {};
    case 0x1CU:
        apb1enr_ = data;
        return {};
    default:
        return std::unexpected(BusError::kPeripheralFault);
    }
}

bool Stm32f1Rcc::is_clock_enabled(uint32_t peripheral_addr) const {
    // APB2 peripherals
    if (peripheral_addr >= 0x40010000U && peripheral_addr < 0x40020000U) {
        // GPIOA-C: bits 2-4, USART1: bit 14, TIM1: bit 11, ADC1: bit 9, AFIO:
        // bit 0
        if (peripheral_addr >= 0x40010800U && peripheral_addr < 0x40010C00U) {
            return ((apb2enr_ >> 2U) & 1U) != 0U; // GPIOA
        }
        if (peripheral_addr >= 0x40010C00U && peripheral_addr < 0x40011000U) {
            return ((apb2enr_ >> 3U) & 1U) != 0U; // GPIOB
        }
        if (peripheral_addr >= 0x40011000U && peripheral_addr < 0x40011400U) {
            return ((apb2enr_ >> 4U) & 1U) != 0U; // GPIOC
        }
        if (peripheral_addr >= 0x40013800U && peripheral_addr < 0x40013C00U) {
            return ((apb2enr_ >> 14U) & 1U) != 0U; // USART1
        }
    }
    // APB1 peripherals
    if (peripheral_addr >= 0x40000000U && peripheral_addr < 0x40010000U) {
        if (peripheral_addr >= 0x40000000U && peripheral_addr < 0x40000400U) {
            return ((apb1enr_ >> 0U) & 1U) != 0U; // TIM2
        }
        if (peripheral_addr >= 0x40000400U && peripheral_addr < 0x40000800U) {
            return ((apb1enr_ >> 1U) & 1U) != 0U; // TIM3
        }
        if (peripheral_addr >= 0x40004400U && peripheral_addr < 0x40004800U) {
            return ((apb1enr_ >> 17U) & 1U) != 0U; // USART2
        }
    }
    return false;
}

void Stm32f1Rcc::enable_clock(uint32_t peripheral_addr) {
    if (peripheral_addr >= 0x40010800U && peripheral_addr < 0x40010C00U) {
        apb2enr_ |= (1U << 2U); // GPIOA
    } else if (peripheral_addr >= 0x40010C00U && peripheral_addr < 0x40011000U) {
        apb2enr_ |= (1U << 3U); // GPIOB
    } else if (peripheral_addr >= 0x40011000U && peripheral_addr < 0x40011400U) {
        apb2enr_ |= (1U << 4U); // GPIOC
    } else if (peripheral_addr >= 0x40013800U && peripheral_addr < 0x40013C00U) {
        apb2enr_ |= (1U << 14U); // USART1
    } else if (peripheral_addr >= 0x40000000U && peripheral_addr < 0x40000400U) {
        apb1enr_ |= (1U << 0U); // TIM2
    } else if (peripheral_addr >= 0x40000400U && peripheral_addr < 0x40000800U) {
        apb1enr_ |= (1U << 1U); // TIM3
    } else if (peripheral_addr >= 0x40004400U && peripheral_addr < 0x40004800U) {
        apb1enr_ |= (1U << 17U); // USART2
    }
}

void Stm32f1Rcc::apply_cr_write(uint32_t data) {
    // Read-only bits: HSIRDY (1), HSERDY (17), PLLRDY (25)
    constexpr uint32_t kRoMask = kHsiRdy | kHseRdy | kPllRdy;
    cr_ = (data & ~kRoMask) | (cr_ & kRoMask);

    // HSION written → keep HSIRDY aligned
    if ((cr_ & kHsiOn) != 0U) {
        cr_ |= kHsiRdy;
    } else {
        cr_ &= ~kHsiRdy;
    }

    // HSEON written → HSERDY immediately ready
    if ((cr_ & kHseOn) != 0U) {
        cr_ |= kHseRdy;
    } else {
        cr_ &= ~kHseRdy;
    }

    // PLLON written → PLLRDY immediately ready
    if ((cr_ & kPllOn) != 0U) {
        cr_ |= kPllRdy;
    } else {
        cr_ &= ~kPllRdy;
    }
}

} // namespace micro_forge::device::stm32f1
