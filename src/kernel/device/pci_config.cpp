/**
 * @file pci_config.cpp
 * @brief Mechanism-1 selector and data window (batch 6d).
 * @version 0.1.0
 */

#include "micro_forge/kernel/device/pci_config.hpp"

namespace micro_forge::kernel {

uint32_t PciFunction::read(uint8_t offset) const {
    return regs_[(offset & 0xFCU) >> 2U];
}

void PciFunction::write(uint8_t offset, uint32_t value) {
    regs_[(offset & 0xFCU) >> 2U] = value;
}

void PciConfig::add_function(uint8_t bus, uint8_t device, uint8_t function,
                             PciFunction& function_space) {
    const uint32_t key = (static_cast<uint32_t>(bus) << 16U) |
                         (static_cast<uint32_t>(device) << 11U) |
                         (static_cast<uint32_t>(function) << 8U);
    functions_[key] = &function_space;
}

PciFunction* PciConfig::current() {
    if ((address_ & 0x80000000U) == 0) {
        return nullptr; // enable bit clear: no device selected
    }
    const uint32_t key = address_ & 0x00FFFF00U; // bus/device/function
    const auto it = functions_.find(key);
    return it != functions_.end() ? it->second : nullptr;
}

bool PciConfig::read(uint16_t offset, unsigned width, uint64_t& out) {
    if (width != 4) {
        return false;
    }
    if (offset == 0x00) { // 0xCF8
        out = address_;
        return true;
    }
    if (offset != 0x04) { // 0xCFC
        return false;
    }
    const PciFunction* const function = current();
    if (function == nullptr) {
        out = 0xFFFFFFFFU; // empty bus: vendor 0xFFFF
        return true;
    }
    // The address register holds the dword index (bits 2..7); the
    // function offsets in bytes.
    const auto register_offset = static_cast<uint8_t>(((address_ >> 2U) & 0x3FU) * 4U);
    out = function->read(register_offset);
    return true;
}

bool PciConfig::write(uint16_t offset, unsigned width, uint64_t value) {
    if (width != 4) {
        return false;
    }
    if (offset == 0x00) {
        address_ = static_cast<uint32_t>(value);
        return true;
    }
    if (offset != 0x04) {
        return false;
    }
    if (PciFunction* function = current(); function != nullptr) {
        const auto register_offset = static_cast<uint8_t>(((address_ >> 2U) & 0x3FU) * 4U);
        function->write(register_offset, static_cast<uint32_t>(value));
    }
    return true;
}

} // namespace micro_forge::kernel
