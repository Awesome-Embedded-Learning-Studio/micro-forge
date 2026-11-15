/**
 * @file memory_access.cpp
 * @brief Converts kernel bus results without retaining a second address map.
 * @version 0.1.0
 */
#include "micro_forge/arch/base/memory_access.hpp"

#include <cstdint>
#include <expected>

#include "micro_forge/kernel/bus/bus_error.hpp"
#include "micro_forge/device/base/types.hpp"

namespace micro_forge::arch {
namespace {

BusError classify(kernel::BusError::Kind kind) {
    using Kind = kernel::BusError::Kind;
    switch (kind) {
    case Kind::kUnmappedRead:
    case Kind::kUnmappedWrite:
        return BusError::kUnmapped;
    case Kind::kOutOfBounds:
    case Kind::kInvalidMapping:
        return BusError::kOutOfRange;
    case Kind::kOverlap:
        return BusError::kRegionOverlap;
    case Kind::kReadOnly:
        return BusError::kReadOnly;
    case Kind::kUnaligned:
        return BusError::kUnaligned;
    case Kind::kDeviceFault:
        return BusError::kPeripheralFault;
    }
    return BusError::kPeripheralFault;
}

template <typename T>
Expected<data_t> read_result(std::expected<T, kernel::BusError> result) {
    if (!result) {
        return std::unexpected(classify(result.error().kind));
    }
    return static_cast<data_t>(*result);
}

} // namespace

Expected<data_t> MemoryAccess::read(addr_t address, Width width) {
    switch (width) {
    case Width::kByte:
        return read_result(bus_.read_u8(address));
    case Width::kHalfWord:
        return read_result(bus_.read_u16(address));
    case Width::kWord:
        return read_result(bus_.read_u32(address));
    }
    return std::unexpected(BusError::kUnaligned);
}

Expected<void> MemoryAccess::write(addr_t address, data_t data, Width width) {
    const auto result = [&] -> std::expected<void, kernel::BusError> {
        switch (width) {
        case Width::kByte:
            return bus_.write_u8(address, static_cast<uint8_t>(data));
        case Width::kHalfWord:
            return bus_.write_u16(address, static_cast<uint16_t>(data));
        case Width::kWord:
            return bus_.write_u32(address, data);
        }
        return std::unexpected(
            kernel::BusError{.kind = kernel::BusError::Kind::kUnaligned, .address = address});
    }();
    if (!result) {
        return std::unexpected(classify(result.error().kind));
    }
    return {};
}

} // namespace micro_forge::arch
