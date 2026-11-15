/**
 * @file device_region.hpp
 * @brief Explicit MMIO, Flash and bit-band adapters for MCU machine assembly.
 * @version 0.1.0
 */
#pragma once

#include <cstdint>
#include <expected>

#include "micro_forge/kernel/bus/bus.hpp"
#include "micro_forge/kernel/memory/ram_region.hpp"
#include "micro_forge/kernel/memory/region.hpp"

#include "micro_forge/device/base/device.hpp"

namespace micro_forge::device {

/** @brief Retains each migrated peripheral's width-specific register semantics. */
class DeviceRegion final : public kernel::Region {
public:
    explicit DeviceRegion(device::Device& device) : device_(device) {}
    std::expected<uint8_t, kernel::BusError> read_u8(uint64_t offset) override;
    std::expected<uint16_t, kernel::BusError> read_u16(uint64_t offset) override;
    std::expected<uint32_t, kernel::BusError> read_u32(uint64_t offset) override;
    std::expected<uint64_t, kernel::BusError> read_u64(uint64_t offset) override;
    std::expected<void, kernel::BusError> write_u8(uint64_t offset, uint8_t value) override;
    std::expected<void, kernel::BusError> write_u16(uint64_t offset, uint16_t value) override;
    std::expected<void, kernel::BusError> write_u32(uint64_t offset, uint32_t value) override;
    std::expected<void, kernel::BusError> write_u64(uint64_t offset, uint64_t value) override;

private:
    device::Device& device_;
};

/** @brief Loader-writable Flash storage that rejects all guest programming writes. */
class FlashRegion final : public kernel::RamRegion {
public:
    explicit FlashRegion(uint64_t size) : RamRegion(size) {}
    std::expected<void, kernel::BusError> write_u8(uint64_t offset, uint8_t value) override;
    std::expected<void, kernel::BusError> write_u16(uint64_t offset, uint16_t value) override;
    std::expected<void, kernel::BusError> write_u32(uint64_t offset, uint32_t value) override;
    std::expected<void, kernel::BusError> write_u64(uint64_t offset, uint64_t value) override;
};

/**
 * @brief Cortex-M bit aliases routed back through the same shared bus.
 *
 * A 32-bit alias access changes one bit of the target's containing word.
 * Other access widths are rejected rather than replayed as byte operations.
 */
class BitBandRegion final : public kernel::Region {
public:
    BitBandRegion(kernel::Bus& bus, uint32_t target_base) : bus_(bus), target_base_(target_base) {}
    std::expected<uint8_t, kernel::BusError> read_u8(uint64_t offset) override;
    std::expected<uint16_t, kernel::BusError> read_u16(uint64_t offset) override;
    std::expected<uint32_t, kernel::BusError> read_u32(uint64_t offset) override;
    std::expected<uint64_t, kernel::BusError> read_u64(uint64_t offset) override;
    std::expected<void, kernel::BusError> write_u8(uint64_t offset, uint8_t value) override;
    std::expected<void, kernel::BusError> write_u16(uint64_t offset, uint16_t value) override;
    std::expected<void, kernel::BusError> write_u32(uint64_t offset, uint32_t value) override;
    std::expected<void, kernel::BusError> write_u64(uint64_t offset, uint64_t value) override;

private:
    kernel::Bus& bus_;
    uint32_t target_base_;
};

} // namespace micro_forge::device
