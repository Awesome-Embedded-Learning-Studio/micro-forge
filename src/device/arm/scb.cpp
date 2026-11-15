#include "micro_forge/device/arm/scb.hpp"
#include "micro_forge/device/base/types.hpp"
#include <cstdint>
#include <expected>

namespace micro_forge::device::arm {

void ScbPeripheral::reset() {
    cpuid_ = 0x412FC230U;
    icsr_ = 0U;
    vtor_ = 0U;
    aircr_ = 0xFA050000U;
    scr_ = 0U;
    ccr_ = 0x00000200U;
    for (auto& priority : shp_) {
        priority = 0U;
    }
    shcsr_ = 0U;
}

Expected<data_t> ScbPeripheral::read(addr_t offset, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }

    switch (offset) {
    case 0x00U:
        return cpuid_;
    case 0x04U:
        return icsr_;
    case 0x08U:
        return vtor_;
    case 0x0CU:
        return aircr_;
    case 0x10U:
        return scr_;
    case 0x14U:
        return ccr_;
    case 0x18U:
        return static_cast<uint32_t>(shp_[0U]) | (shp_[1U] << 8U) | (shp_[2U] << 16U) |
               (shp_[3U] << 24U);
    case 0x1CU:
        return static_cast<uint32_t>(shp_[4U]) | (shp_[5U] << 8U) | (shp_[6U] << 16U) |
               (shp_[7U] << 24U);
    case 0x20U:
        return static_cast<uint32_t>(shp_[8U]) | (shp_[9U] << 8U) | (shp_[10U] << 16U) |
               (shp_[11U] << 24U);
    case 0x24U:
        return shcsr_;
    default:
        return std::unexpected(BusError::kPeripheralFault);
    }
}

Expected<void> ScbPeripheral::write(addr_t offset, data_t data, Width w) {
    // SHP registers (offsets 0x18-0x23) support byte writes
    if (w == Width::kByte && offset >= 0x18U && offset <= 0x23U) {
        shp_[offset - 0x18U] = data & 0xFFU;
        return {};
    }

    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }

    switch (offset) {
    case 0x04U:
        icsr_ = data;
        return {};
    case 0x08U:
        vtor_ = data;
        if (vtor_cb_) {
            vtor_cb_(data);
        }
        return {};
    case 0x0CU:
        // AIRCR: must write VECTKEY = 0x05FA in upper 16 bits
        if ((data >> 16U) != 0x05FAU) {
            // ARM specifies writes with the wrong key are ignored; this is
            // an intentional hardware-compatible no-op, not a hidden fault.
            return {};
        }
        aircr_ = (data & 0x0000FFFFU) | 0xFA050000U;
        if (prigroup_cb_) {
            // PRIGROUP occupies AIRCR bits [10:8].
            prigroup_cb_(static_cast<uint8_t>((aircr_ >> 8U) & 0x7U));
        }
        return {};
    case 0x10U:
        scr_ = data;
        return {};
    case 0x14U:
        ccr_ = data;
        return {};
    case 0x18U:
        shp_[0U] = data & 0xFFU;
        shp_[1U] = (data >> 8U) & 0xFFU;
        shp_[2U] = (data >> 16U) & 0xFFU;
        shp_[3U] = (data >> 24U) & 0xFFU;
        return {};
    case 0x1CU:
        shp_[4U] = data & 0xFFU;
        shp_[5U] = (data >> 8U) & 0xFFU;
        shp_[6U] = (data >> 16U) & 0xFFU;
        shp_[7U] = (data >> 24U) & 0xFFU;
        return {};
    case 0x20U:
        shp_[8U] = data & 0xFFU;
        shp_[9U] = (data >> 8U) & 0xFFU;
        shp_[10U] = (data >> 16U) & 0xFFU;
        shp_[11U] = (data >> 24U) & 0xFFU;
        return {};
    case 0x24U:
        shcsr_ = data;
        return {};
    default:
        return std::unexpected(BusError::kPeripheralFault);
    }
}

} // namespace micro_forge::device::arm
