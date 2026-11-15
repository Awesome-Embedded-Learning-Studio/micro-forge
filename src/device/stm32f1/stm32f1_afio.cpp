#include "micro_forge/device/stm32f1/stm32f1_afio.hpp"
#include "micro_forge/device/base/types.hpp"
#include <cstdint>
#include <expected>

namespace micro_forge::device::stm32f1 {

void Stm32f1Afio::reset() {
    evcr_ = 0U;
    mapr_ = 0U;
    for (auto& selection : exticr_) {
        selection = 0U;
    }
    mapr2_ = 0U;
}

Expected<data_t> Stm32f1Afio::read(addr_t offset, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }
    switch (offset) {
    case 0x00U:
        return evcr_;
    case 0x04U:
        return mapr_;
    case 0x08U:
        return exticr_[0U];
    case 0x0CU:
        return exticr_[1U];
    case 0x10U:
        return exticr_[2U];
    case 0x14U:
        return exticr_[3U];
    case 0x18U:
        return mapr2_;
    default:
        // STM32 reserved MMIO locations are modeled as read-as-zero so HAL
        // feature probes do not fault on harmless compatibility reads.
        return 0U;
    }
}

Expected<void> Stm32f1Afio::write(addr_t offset, data_t data, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }
    switch (offset) {
    case 0x00U:
        evcr_ = data;
        return {};
    case 0x04U:
        mapr_ = data;
        return {};
    case 0x08U:
        exticr_[0U] = data;
        return {};
    case 0x0CU:
        exticr_[1U] = data;
        return {};
    case 0x10U:
        exticr_[2U] = data;
        return {};
    case 0x14U:
        exticr_[3U] = data;
        return {};
    case 0x18U:
        mapr2_ = data;
        return {};
    default:
        // Reserved writes are ignored to match peripheral compatibility
        // behavior expected by vendor HAL initialization paths.
        return {};
    }
}

uint8_t Stm32f1Afio::exti_line_port(uint8_t line) const {
    if (line > 15U) {
        return 0U;
    }
    return (exticr_[line / 4U] >> ((line % 4U) * 4U)) & 0xFU;
}

} // namespace micro_forge::device::stm32f1
