#include "micro_forge/device/stm32f1/stm32f1_flash.hpp"
#include "micro_forge/device/base/types.hpp"
#include <expected>

namespace micro_forge::device::stm32f1 {

void Stm32f1Flash::reset() {
    acr_ = 0x00000030U;
    keyr_ = 0U;
    optkeyr_ = 0U;
    sr_ = 0U;
    cr_ = 0x00000080U;
}

Expected<data_t> Stm32f1Flash::read(addr_t offset, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }
    switch (offset) {
    case 0x00U:
        return acr_;
    case 0x04U:
        return keyr_;
    case 0x08U:
        return optkeyr_;
    case 0x0CU:
        return sr_;
    case 0x10U:
        return cr_;
    default:
        // STM32 reserved MMIO locations are modeled as read-as-zero so HAL
        // feature probes do not fault on harmless compatibility reads.
        return 0U;
    }
}

Expected<void> Stm32f1Flash::write(addr_t offset, data_t data, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }
    switch (offset) {
    case 0x00U:
        acr_ = data;
        return {};
    case 0x04U:
        keyr_ = data;
        return {}; // Accept write, no unlock logic
    case 0x08U:
        optkeyr_ = data;
        return {};
    case 0x0CU:
        sr_ = data;
        return {}; // W1C bits accepted
    case 0x10U:
        cr_ = data;
        return {};
    default:
        // Reserved writes are ignored to match peripheral compatibility
        // behavior expected by vendor HAL initialization paths.
        return {};
    }
}

} // namespace micro_forge::device::stm32f1
