/**
 * @file stm32f103_regions.cpp
 * @brief Width-preserving MCU register and alias windows on the shared bus.
 * @version 0.1.0
 */
#include "micro_forge/device/base/device_region.hpp"

#include <cstdint>
#include <expected>

#include "micro_forge/device/base/device.hpp"
#include "micro_forge/device/base/types.hpp"
#include "micro_forge/kernel/bus/bus_error.hpp"

namespace micro_forge::device {
namespace {

std::unexpected<kernel::BusError> reject(uint64_t offset, kernel::BusError::Kind kind) {
    return std::unexpected(kernel::BusError{.kind = kind, .address = offset});
}

kernel::BusError::Kind classify(BusError error) {
    using Kind = kernel::BusError::Kind;
    switch (error) {
    case BusError::kUnaligned:
        return Kind::kUnaligned;
    case BusError::kReadOnly:
        return Kind::kReadOnly;
    case BusError::kOutOfRange:
        return Kind::kOutOfBounds;
    default:
        return Kind::kDeviceFault;
    }
}

template <typename Value>
std::expected<Value, kernel::BusError> read_device(device::Device& device, uint64_t offset,
                                                   Width width) {
    if (offset % sizeof(Value) != 0) {
        return reject(offset, kernel::BusError::Kind::kUnaligned);
    }
    const auto value = device.read(static_cast<addr_t>(offset), width);
    if (!value) {
        return reject(offset, classify(value.error()));
    }
    return static_cast<Value>(*value);
}

std::expected<void, kernel::BusError> write_device(device::Device& device, uint64_t offset,
                                                   uint32_t value, Width width) {
    if (offset % static_cast<uint32_t>(width) != 0) {
        return reject(offset, kernel::BusError::Kind::kUnaligned);
    }
    const auto result = device.write(static_cast<addr_t>(offset), value, width);
    if (!result) {
        return reject(offset, classify(result.error()));
    }
    return {};
}

} // namespace

std::expected<uint8_t, kernel::BusError> DeviceRegion::read_u8(uint64_t offset) {
    return read_device<uint8_t>(device_, offset, Width::kByte);
}
std::expected<uint16_t, kernel::BusError> DeviceRegion::read_u16(uint64_t offset) {
    return read_device<uint16_t>(device_, offset, Width::kHalfWord);
}
std::expected<uint32_t, kernel::BusError> DeviceRegion::read_u32(uint64_t offset) {
    return read_device<uint32_t>(device_, offset, Width::kWord);
}
std::expected<uint64_t, kernel::BusError> DeviceRegion::read_u64(uint64_t offset) {
    return reject(offset, kernel::BusError::Kind::kUnaligned);
}
std::expected<void, kernel::BusError> DeviceRegion::write_u8(uint64_t offset, uint8_t value) {
    return write_device(device_, offset, value, Width::kByte);
}
std::expected<void, kernel::BusError> DeviceRegion::write_u16(uint64_t offset, uint16_t value) {
    return write_device(device_, offset, value, Width::kHalfWord);
}
std::expected<void, kernel::BusError> DeviceRegion::write_u32(uint64_t offset, uint32_t value) {
    return write_device(device_, offset, value, Width::kWord);
}
std::expected<void, kernel::BusError> DeviceRegion::write_u64(uint64_t offset, uint64_t /*value*/) {
    return reject(offset, kernel::BusError::Kind::kUnaligned);
}

std::expected<void, kernel::BusError> FlashRegion::write_u8(uint64_t offset, uint8_t /*value*/) {
    return reject(offset, kernel::BusError::Kind::kReadOnly);
}
std::expected<void, kernel::BusError> FlashRegion::write_u16(uint64_t offset, uint16_t /*value*/) {
    return reject(offset, kernel::BusError::Kind::kReadOnly);
}
std::expected<void, kernel::BusError> FlashRegion::write_u32(uint64_t offset, uint32_t /*value*/) {
    return reject(offset, kernel::BusError::Kind::kReadOnly);
}
std::expected<void, kernel::BusError> FlashRegion::write_u64(uint64_t offset, uint64_t /*value*/) {
    return reject(offset, kernel::BusError::Kind::kReadOnly);
}

std::expected<uint32_t, kernel::BusError> BitBandRegion::read_u32(uint64_t offset) {
    if ((offset & 3U) != 0) {
        return reject(offset, kernel::BusError::Kind::kUnaligned);
    }
    const uint64_t byte = target_base_ + (offset >> 5U);
    const auto bit = static_cast<unsigned>(((byte & 3U) * 8U) + ((offset >> 2U) & 7U));
    const auto word = bus_.read_u32(byte & ~uint64_t{3});
    if (!word) {
        return reject(offset, word.error().kind);
    }
    return (*word >> bit) & 1U;
}

std::expected<void, kernel::BusError> BitBandRegion::write_u32(uint64_t offset, uint32_t value) {
    if ((offset & 3U) != 0) {
        return reject(offset, kernel::BusError::Kind::kUnaligned);
    }
    const uint64_t byte = target_base_ + (offset >> 5U);
    const auto bit = static_cast<unsigned>(((byte & 3U) * 8U) + ((offset >> 2U) & 7U));
    const uint64_t address = byte & ~uint64_t{3};
    const auto word = bus_.read_u32(address);
    if (!word) {
        return reject(offset, word.error().kind);
    }
    const uint32_t mask = uint32_t{1} << bit;
    const auto result = bus_.write_u32(address, (value & 1U) != 0 ? *word | mask : *word & ~mask);
    if (!result) {
        return reject(offset, result.error().kind);
    }
    return {};
}

std::expected<uint8_t, kernel::BusError> BitBandRegion::read_u8(uint64_t offset) {
    return reject(offset, kernel::BusError::Kind::kUnaligned);
}
std::expected<uint16_t, kernel::BusError> BitBandRegion::read_u16(uint64_t offset) {
    return reject(offset, kernel::BusError::Kind::kUnaligned);
}
std::expected<uint64_t, kernel::BusError> BitBandRegion::read_u64(uint64_t offset) {
    return reject(offset, kernel::BusError::Kind::kUnaligned);
}
std::expected<void, kernel::BusError> BitBandRegion::write_u8(uint64_t offset, uint8_t /*value*/) {
    return reject(offset, kernel::BusError::Kind::kUnaligned);
}
std::expected<void, kernel::BusError> BitBandRegion::write_u16(uint64_t offset,
                                                               uint16_t /*value*/) {
    return reject(offset, kernel::BusError::Kind::kUnaligned);
}
std::expected<void, kernel::BusError> BitBandRegion::write_u64(uint64_t offset,
                                                               uint64_t /*value*/) {
    return reject(offset, kernel::BusError::Kind::kUnaligned);
}

} // namespace micro_forge::device
