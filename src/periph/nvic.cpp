#include "periph/nvic.hpp"

namespace micro_forge::periph {

// MMIO register layout (offsets relative to NVIC base 0xE000E100):
//   0x000-0x01F  ISER0-7   (Set Enable)
//   0x080-0x09F  ICER0-7   (Clear Enable — writes clear ISER bits)
//   0x100-0x11F  ISPR0-7   (Set Pending)
//   0x180-0x19F  ICPR0-7   (Clear Pending — writes clear ISPR bits)
//   0x300-0x4F0  IP0-59    (Priority, 4 per word)

Expected<data_t> NvicPeripheral::read(addr_t offset, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }

    if (offset < 0x020) { // ISER
        size_t idx = offset / 4;
        if (idx >= iser_.size()) {
            return std::unexpected(BusError::Unmapped);
        }
        return iser_[idx];
    }
    if (offset >= 0x080 && offset < 0x0A0) { // ICER (reads ISER)
        size_t idx = (offset - 0x080) / 4;
        if (idx >= iser_.size()) {
            return std::unexpected(BusError::Unmapped);
        }
        return iser_[idx];
    }
    if (offset >= 0x100 && offset < 0x120) { // ISPR
        size_t idx = (offset - 0x100) / 4;
        if (idx >= ispr_.size()) {
            return std::unexpected(BusError::Unmapped);
        }
        return ispr_[idx];
    }
    if (offset >= 0x180 && offset < 0x1A0) { // ICPR (reads ISPR)
        size_t idx = (offset - 0x180) / 4;
        if (idx >= ispr_.size()) {
            return std::unexpected(BusError::Unmapped);
        }
        return ispr_[idx];
    }
    if (offset >= 0x300 && offset < 0x500) { // IP priority
        size_t word_idx = (offset - 0x300) / 4;
        size_t base_irq = word_idx * 4;
        if (base_irq >= kMaxIrq) {
            return std::unexpected(BusError::Unmapped);
        }
        data_t val = 0;
        for (size_t i = 0; i < 4 && (base_irq + i) < kMaxIrq; ++i) {
            val |= static_cast<data_t>(priorities_[base_irq + i]) << (i * 8);
        }
        return val;
    }

    return std::unexpected(BusError::Unmapped);
}

Expected<void> NvicPeripheral::write(addr_t offset, data_t data, Width w) {
    // IPR(优先级寄存器)支持任意 width:ARMv7-M NVIC 优先级是字节寄存器,
    // HAL_NVIC_SetPriority 用 strb(字节写)IP[IRQn]。先处理 IPR。
    if (offset >= 0x300 && offset < 0x500) { // IP priority
        size_t byte_idx = offset - 0x300;
        size_t nbytes = static_cast<size_t>(w);
        for (size_t i = 0; i < nbytes && (byte_idx + i) < kMaxIrq; ++i) {
            priorities_[byte_idx + i] =
                static_cast<uint8_t>((data >> (i * 8)) & 0xFF);
        }
        invalidate_cache();
        return {};
    }

    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }

    if (offset < 0x020) { // ISER (set bits)
        size_t idx = offset / 4;
        if (idx >= iser_.size()) {
            return std::unexpected(BusError::Unmapped);
        }
        iser_[idx] |= data;
        invalidate_cache();
        return {};
    }
    if (offset >= 0x080 && offset < 0x0A0) { // ICER (clear ISER bits)
        size_t idx = (offset - 0x080) / 4;
        if (idx >= iser_.size()) {
            return std::unexpected(BusError::Unmapped);
        }
        iser_[idx] &= ~data;
        invalidate_cache();
        return {};
    }
    if (offset >= 0x100 && offset < 0x120) { // ISPR (set bits)
        size_t idx = (offset - 0x100) / 4;
        if (idx >= ispr_.size()) {
            return std::unexpected(BusError::Unmapped);
        }
        ispr_[idx] |= data;
        invalidate_cache();
        return {};
    }
    if (offset >= 0x180 && offset < 0x1A0) { // ICPR (clear ISPR bits)
        size_t idx = (offset - 0x180) / 4;
        if (idx >= ispr_.size()) {
            return std::unexpected(BusError::Unmapped);
        }
        ispr_[idx] &= ~data;
        invalidate_cache();
        return {};
    }
    return std::unexpected(BusError::Unmapped);
}

} // namespace micro_forge::periph
