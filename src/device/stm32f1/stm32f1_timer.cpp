#include "micro_forge/device/stm32f1/stm32f1_timer.hpp"
#include "micro_forge/device/base/types.hpp"
#include <cstdint>
#include <expected>

namespace micro_forge::device::stm32f1 {

void Stm32f1Timer::reset() {
    cr1_ = 0U;
    cr2_ = 0U;
    smcr_ = 0U;
    dier_ = 0U;
    sr_ = 0U;
    psc_ = 0U;
    arr_ = 0xFFFFU;
    cnt_ = 0U;
    prescaler_residual_ = 0U;
}

Expected<data_t> Stm32f1Timer::read(addr_t offset, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }
    switch (offset) {
    case 0x00U:
        return cr1_;
    case 0x04U:
        return cr2_;
    case 0x08U:
        return smcr_;
    case 0x0CU:
        return dier_;
    case 0x10U:
        return sr_;
    case 0x14U:
        return 0U; // EGR bits are write-only, read as zero.
    case 0x24U:
        return cnt_;
    case 0x28U:
        return psc_;
    case 0x2CU:
        return arr_;
    default:
        return std::unexpected(BusError::kPeripheralFault);
    }
}

Expected<void> Stm32f1Timer::write(addr_t offset, data_t data, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }
    switch (offset) {
    case 0x00U:
        cr1_ = data;
        return {};
    case 0x04U:
        cr2_ = data;
        return {};
    case 0x08U:
        smcr_ = data;
        return {};
    case 0x0CU:
        dier_ = data;
        return {};
    case 0x10U:
        sr_ = data;
        return {};
    case 0x14U:
        if ((data & 1U) != 0U) { // UG reinitializes counter and prescaler phase.
            cnt_ = 0U;
            prescaler_residual_ = 0U;
            // URS suppresses software-generated update requests.
            if ((cr1_ & (1U << 2U)) == 0U) {
                generate_update();
            }
        }
        return {};
    case 0x24U:
        cnt_ = data & 0xFFFFU;
        return {};
    case 0x28U:
        psc_ = data & 0xFFFFU;
        return {};
    case 0x2CU:
        arr_ = data & 0xFFFFU;
        return {};
    default:
        return std::unexpected(BusError::kPeripheralFault);
    }
}

void Stm32f1Timer::generate_update() {
    if ((cr1_ & (1U << 1U)) != 0U) { // UDIS disables update events.
        return;
    }
    if ((sr_ & 1U) == 0U) {
        sr_ |= 1U;
        if (((dier_ & 1U) != 0U) && irq_cb_) {
            irq_cb_();
        }
    }
}

void Stm32f1Timer::tick(uint64_t cycles) {
    if ((cr1_ & 1U) == 0U) {
        return;
    }

    const uint64_t divisor = static_cast<uint64_t>(psc_) + 1U;
    const uint64_t remainder = (cycles % divisor) + prescaler_residual_;
    const uint64_t prescaled = (cycles / divisor) + (remainder / divisor);
    prescaler_residual_ = remainder % divisor;
    if (prescaled == 0U) {
        return;
    }
    const uint64_t period = static_cast<uint64_t>(arr_) + 1U;
    // Split the addition so even a full uint64_t cycle advance cannot wrap.
    const uint64_t partial = cnt_ + (prescaled % period);
    const bool overflow = prescaled >= period || partial >= period;
    cnt_ = static_cast<uint32_t>(partial % period);
    if (overflow) {
        generate_update();
    }
}

void Stm32f1Timer::enable(bool en) {
    if (en) {
        cr1_ |= 1U;
    } else {
        cr1_ &= ~1U;
        prescaler_residual_ = 0U;
    }
}

void Stm32f1Timer::set_prescaler(uint32_t psc) {
    psc_ = psc & 0xFFFFU;
    prescaler_residual_ = 0U;
}

void Stm32f1Timer::set_auto_reload(uint32_t arr) {
    arr_ = arr & 0xFFFFU;
}

uint32_t Stm32f1Timer::counter() const {
    return cnt_;
}
bool Stm32f1Timer::update_flag() const {
    return (sr_ & 1U) != 0U;
}
void Stm32f1Timer::clear_update_flag() {
    sr_ &= ~1U;
}

} // namespace micro_forge::device::stm32f1
