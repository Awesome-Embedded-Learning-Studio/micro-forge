/**
 * @file io_apic.cpp
 * @brief IO-APIC indirect register semantics (batch 6c).
 * @version 0.1.0
 */

#include "micro_forge/kernel/device/io_apic.hpp"

#include "micro_forge/kernel/le_access.hpp"

namespace micro_forge::kernel {

IoApic::IoApic() : RegisterRegionBase(window_.data(), window_.size()) {
    registers_[kId] = 0;
    registers_[kVersion] = 0x11U | (23U << 16U); // 24 redirection entries
}

std::array<uint32_t, 2> IoApic::redirection(unsigned entry) const {
    const uint32_t base = kRedirBase + (entry * 2U);
    if (base + 1U >= registers_.size()) {
        return {};
    }
    return {registers_[base], registers_[base + 1U]};
}

std::expected<uint8_t, BusError> IoApic::read_u8(uint64_t offset) {
    const auto value = read_u32(offset & ~3ULL);
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    const unsigned shift = static_cast<unsigned>(offset & 3U) * 8U;
    return static_cast<uint8_t>(*value >> shift);
}

std::expected<uint16_t, BusError> IoApic::read_u16(uint64_t offset) {
    const auto value = read_u32(offset & ~3ULL);
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    const unsigned shift = static_cast<unsigned>(offset & 2U) * 8U;
    return static_cast<uint16_t>(*value >> shift);
}

std::expected<uint32_t, BusError> IoApic::read_u32(uint64_t offset) {
    if (offset == 0x00) { // IOREGSEL
        return selected_;
    }
    if (offset == 0x10) { // IOWIN
        if (selected_ >= registers_.size()) {
            return 0;
        }
        return registers_[selected_];
    }
    return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = offset});
}

std::expected<void, BusError> IoApic::write_u8(uint64_t offset, uint8_t value) {
    return write_u32(offset & ~3ULL, static_cast<uint32_t>(value) << (8U * (offset & 3U)));
}

std::expected<void, BusError> IoApic::write_u16(uint64_t offset, uint16_t value) {
    return write_u32(offset & ~3ULL, static_cast<uint32_t>(value) << (8U * (offset & 2U)));
}

std::expected<void, BusError> IoApic::write_u32(uint64_t offset, uint32_t value) {
    if (offset == 0x00) {
        selected_ = value;
        return {};
    }
    if (offset == 0x10) {
        if (selected_ < registers_.size()) {
            registers_[selected_] = value;
        }
        return {};
    }
    return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = offset});
}

} // namespace micro_forge::kernel
