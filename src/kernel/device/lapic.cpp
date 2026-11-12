/**
 * @file lapic.cpp
 * @brief Local APIC register semantics (batch 6c).
 * @version 0.1.0
 */

#include "micro_forge/kernel/device/lapic.hpp"

#include "micro_forge/kernel/le_access.hpp"

namespace micro_forge::kernel {

Lapic::Lapic() : RegisterRegionBase(window_.data(), window_.size()) {
    // ID 0, version 0x14 | max LVT 4 (the boot path only reads it back).
    put_le32(window_.data() + kId, 0);
    put_le32(window_.data() + kVersion, 0x14U | (4U << 16U));
    put_le32(window_.data() + kSpurious, 0x1FFU); // spurious vector, software-off
}

void Lapic::on_read(uint64_t offset, unsigned width) {
    (void)width;
    if (offset == kTimerCurrent) {
        // The timer counts down on the virtual clock: one count per
        // retired instruction (a 1:1 mapping until the tick pipeline
        // owns the frequency in batch 6d).
        uint32_t remaining = timer_initial_;
        if (now_ && timer_initial_ != 0) {
            const uint64_t elapsed = now_() - timer_start_;
            remaining =
                elapsed >= timer_initial_ ? 0U : static_cast<uint32_t>(timer_initial_ - elapsed);
        }
        put_le32(window_.data() + kTimerCurrent, remaining);
    }
}

void Lapic::on_write(uint64_t offset, unsigned width, uint64_t value) {
    if (width != 4) {
        return;
    }
    const auto word = static_cast<uint32_t>(value);
    if (offset == kEoi) {
        eoi_seen_ = true; // EOI is a self-clearing write-only register
        put_le32(window_.data() + kEoi, 0);
        return;
    }
    if (offset == kTimerInitial) {
        timer_initial_ = word;
        timer_start_ = now_ ? now_() : 0;
        put_le32(window_.data() + kTimerCurrent, word);
        return;
    }
    if (offset == kTimerDivide) {
        timer_divide_ = word;
    }
}

} // namespace micro_forge::kernel
