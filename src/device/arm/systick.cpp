#include "micro_forge/device/arm/systick.hpp"
#include "micro_forge/device/base/types.hpp"
#include <cstdint>
#include <expected>

namespace micro_forge::device::arm {

void SysTickPeripheral::reset() {
    ctrl_ = 0U;
    core_.reset();
}

// Registers are relative to SysTick base 0xE000E010.
Expected<data_t> SysTickPeripheral::read(addr_t offset, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }

    switch (offset) {
    case 0x00U: {
        constexpr uint32_t kCountFlag = 1U << 16U;
        const auto value = ctrl_ | (core_.expired() ? kCountFlag : 0U);
        core_.clear_expired(); // Reading CTRL clears COUNTFLAG.
        return value;
    }
    case 0x04U:
        return core_.reload();
    case 0x08U:
        return core_.value();
    case 0x0CU:
        return 0U; // CALIB
    default:
        return std::unexpected(BusError::kUnmapped);
    }
}

Expected<void> SysTickPeripheral::write(addr_t offset, data_t data, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }

    switch (offset) {
    case 0x00U:
        // COUNTFLAG lives in the core and is read-only. ENABLE does not
        // preload the counter.
        ctrl_ = data & 0x7U;
        return {};
    case 0x04U:
        core_.set_reload(data);
        return {};
    case 0x08U:
        core_.clear_value();
        return {};
    case 0x0CU:
        return std::unexpected(BusError::kReadOnly);
    default:
        return std::unexpected(BusError::kUnmapped);
    }
}

void SysTickPeripheral::tick(uint64_t cycles) {
    if ((ctrl_ & 0x1U) == 0U) {
        return;
    }

    // The machine supplies SysTick input-clock cycles, including CLKSOURCE
    // division. Wrap gating (TICKINT) is applied by the expire callback.
    core_.tick(cycles);
}

} // namespace micro_forge::device::arm
