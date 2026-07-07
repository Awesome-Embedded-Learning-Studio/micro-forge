#include "chips/stm32f1/periph/stm32f1_timer.hpp"

namespace micro_forge::chips::stm32f1 {

Expected<data_t> Stm32f1Timer::read(addr_t offset, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }

    switch (offset) {
        case 0x00:
            return cr1_;
        case 0x0C:
            return dier_;
        case 0x10:
            return sr_;
        case 0x28:
            return psc_;
        case 0x2C:
            return arr_;
        case 0x34:
            return cnt_;
        default:
            return std::unexpected(BusError::PeripheralFault);
    }
}

Expected<void> Stm32f1Timer::write(addr_t offset, data_t data, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }

    switch (offset) {
        case 0x00:
            cr1_ = data;
            return {};
        case 0x0C:
            dier_ = data;
            return {};
        case 0x10:
            sr_ = data;
            return {};
        case 0x28:
            psc_ = data;
            return {};
        case 0x2C:
            arr_ = data;
            return {};
        case 0x34:
            cnt_ = data;
            return {};
        default:
            return std::unexpected(BusError::PeripheralFault);
    }
}

void Stm32f1Timer::tick(uint64_t cycles) {
    if (!(cr1_ & 0x0001)) {
        return; // CEN not set
    }

    const uint64_t divisor = static_cast<uint64_t>(psc_) + 1;
    const uint64_t total_cycles = cycles + prescaler_residual_;
    uint64_t prescaled = total_cycles / divisor;
    prescaler_residual_ = total_cycles % divisor;
    cnt_ += static_cast<uint32_t>(prescaled);

    if (arr_ > 0 && cnt_ >= arr_) {
        cnt_ = cnt_ % arr_;
        // Update event: set UIF on the 0→1 edge only. With DIER.UIE (bit0)
        // enabled, raise the TIM IRQ once per edge (NVIC set_pending is
        // idempotent, but edge semantics avoid re-raising while UIF stays set).
        if (!(sr_ & 0x0001u)) {
            sr_ |= 0x0001u; // UIF
            if ((dier_ & 0x0001u) && irq_cb_) {
                irq_cb_();
            }
        }
    }
}

void Stm32f1Timer::enable(bool en) {
    if (en) {
        cr1_ |= 0x0001;
    } else {
        cr1_ &= ~0x0001u;
        prescaler_residual_ = 0;
    }
}

void Stm32f1Timer::set_prescaler(uint32_t psc) {
    psc_ = psc;
    prescaler_residual_ = 0;
}
void Stm32f1Timer::set_auto_reload(uint32_t arr) {
    arr_ = arr;
}
uint32_t Stm32f1Timer::counter() const {
    return cnt_;
}
bool Stm32f1Timer::update_flag() const {
    return (sr_ & 0x0001) != 0;
}
void Stm32f1Timer::clear_update_flag() {
    sr_ &= ~0x0001u;
}

} // namespace micro_forge::chips::stm32f1
