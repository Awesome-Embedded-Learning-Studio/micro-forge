#include "chips/stm32f1/stm32f1_flash.hpp"

namespace micro_forge::chips::stm32f1 {

Expected<data_t> Stm32f1Flash::read(addr_t offset, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }
    switch (offset) {
        case 0x00:
            return acr_;
        case 0x04:
            return keyr_;
        case 0x08:
            return optkeyr_;
        case 0x0C:
            return sr_;
        case 0x10:
            return cr_;
        default:
            // STM32 reserved MMIO locations are modeled as read-as-zero so HAL
            // feature probes do not fault on harmless compatibility reads.
            return 0u;
    }
}

Expected<void> Stm32f1Flash::write(addr_t offset, data_t data, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }
    switch (offset) {
        case 0x00:
            acr_ = data;
            return {};
        case 0x04:
            keyr_ = data;
            return {}; // Accept write, no unlock logic
        case 0x08:
            optkeyr_ = data;
            return {};
        case 0x0C:
            sr_ = data;
            return {}; // W1C bits accepted
        case 0x10:
            cr_ = data;
            return {};
        default:
            // Reserved writes are ignored to match peripheral compatibility
            // behavior expected by vendor HAL initialization paths.
            return {};
    }
}

} // namespace micro_forge::chips::stm32f1
