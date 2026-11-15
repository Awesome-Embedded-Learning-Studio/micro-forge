#include "micro_forge/device/arm/nvic.hpp"
#include "micro_forge/device/base/types.hpp"
#include <cstddef>
#include <cstdint>
#include <expected>

namespace micro_forge::device::arm {

void NvicPeripheral::reset() {
    iser_.fill(0U);
    ispr_.fill(0U);
    priorities_.fill(0U);
    cached_best_irq_ = 0xFFU;
    cached_best_pri_ = 0xFFU;
    cache_valid_ = false;
}

// MMIO register layout (offsets relative to NVIC base 0xE000E100):
//   0x000-0x01F  ISER0-7   (Set Enable)
//   0x080-0x09F  ICER0-7   (Clear Enable — writes clear ISER bits)
//   0x100-0x11F  ISPR0-7   (Set Pending)
//   0x180-0x19F  ICPR0-7   (Clear Pending — writes clear ISPR bits)
//   0x300-0x4F0  IP0-59    (Priority, 4 per word)

Expected<data_t> NvicPeripheral::read(addr_t offset, Width w) {
    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }

    if (offset < 0x020U) { // ISER
        size_t const idx = offset / 4U;
        if (idx >= iser_.size()) {
            return std::unexpected(BusError::kUnmapped);
        }
        return iser_[idx];
    }
    if (offset >= 0x080U && offset < 0x0A0U) { // ICER (reads ISER)
        size_t const idx = (offset - 0x080U) / 4U;
        if (idx >= iser_.size()) {
            return std::unexpected(BusError::kUnmapped);
        }
        return iser_[idx];
    }
    if (offset >= 0x100U && offset < 0x120U) { // ISPR
        size_t const idx = (offset - 0x100U) / 4U;
        if (idx >= ispr_.size()) {
            return std::unexpected(BusError::kUnmapped);
        }
        return ispr_[idx];
    }
    if (offset >= 0x180U && offset < 0x1A0U) { // ICPR (reads ISPR)
        size_t const idx = (offset - 0x180U) / 4U;
        if (idx >= ispr_.size()) {
            return std::unexpected(BusError::kUnmapped);
        }
        return ispr_[idx];
    }
    if (offset >= 0x300U && offset < 0x500U) { // IP priority
        size_t const word_idx = (offset - 0x300U) / 4U;
        size_t const base_irq = word_idx * 4U;
        if (base_irq >= kMaxIrq) {
            return std::unexpected(BusError::kUnmapped);
        }
        data_t val = 0U;
        for (size_t i = 0U; i < 4U && (base_irq + i) < kMaxIrq; ++i) {
            val |= static_cast<data_t>(priorities_[base_irq + i]) << (i * 8U);
        }
        return val;
    }

    return std::unexpected(BusError::kUnmapped);
}

Expected<void> NvicPeripheral::write(addr_t offset, data_t data, Width w) {
    // IPR(优先级寄存器)支持任意 width:ARMv7-M NVIC 优先级是字节寄存器,
    // HAL_NVIC_SetPriority 用 strb(字节写)IP[IRQn]。先处理 IPR。
    if (offset >= 0x300U && offset < 0x500U) { // IP priority
        size_t const byte_idx = offset - 0x300U;
        auto const nbytes = static_cast<size_t>(w);
        for (size_t i = 0U; i < nbytes && (byte_idx + i) < kMaxIrq; ++i) {
            priorities_[byte_idx + i] = static_cast<uint8_t>((data >> (i * 8U)) & 0xFFU);
        }
        invalidate_cache();
        return {};
    }

    if (w != Width::kWord) {
        return std::unexpected(BusError::kUnaligned);
    }

    if (offset < 0x020U) { // ISER (set bits)
        size_t const idx = offset / 4U;
        if (idx >= iser_.size()) {
            return std::unexpected(BusError::kUnmapped);
        }
        iser_[idx] |= data;
        invalidate_cache();
        return {};
    }
    if (offset >= 0x080U && offset < 0x0A0U) { // ICER (clear ISER bits)
        size_t const idx = (offset - 0x080U) / 4U;
        if (idx >= iser_.size()) {
            return std::unexpected(BusError::kUnmapped);
        }
        iser_[idx] &= ~data;
        invalidate_cache();
        return {};
    }
    if (offset >= 0x100U && offset < 0x120U) { // ISPR (set bits)
        size_t const idx = (offset - 0x100U) / 4U;
        if (idx >= ispr_.size()) {
            return std::unexpected(BusError::kUnmapped);
        }
        ispr_[idx] |= data;
        invalidate_cache();
        return {};
    }
    if (offset >= 0x180U && offset < 0x1A0U) { // ICPR (clear ISPR bits)
        size_t const idx = (offset - 0x180U) / 4U;
        if (idx >= ispr_.size()) {
            return std::unexpected(BusError::kUnmapped);
        }
        ispr_[idx] &= ~data;
        invalidate_cache();
        return {};
    }
    return std::unexpected(BusError::kUnmapped);
}

} // namespace micro_forge::device::arm
