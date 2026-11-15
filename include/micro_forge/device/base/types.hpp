/**
 * @file types.hpp
 * @brief Explicit 32-bit MCU types used by the migrated Cortex-M components.
 * @version 0.1.0
 */
#pragma once

#include <cstdint>
#include <expected>

namespace micro_forge::device {

using addr_t = uint32_t;      ///< Cortex-M address, independent of the host and x86 guest.
using data_t = uint32_t;      ///< Cortex-M general-purpose register value.
using half_word_t = uint16_t; ///< One Thumb instruction halfword.
using byte_t = uint8_t;       ///< One addressed byte.

/** @brief Errors retained by the MCU instruction and peripheral models. */
enum class BusError : uint8_t {
    kUnmapped,
    kUnaligned,
    kReadOnly,
    kInvalidDevice,
    kRegionOverlap,
    kOutOfRange,
    kPeripheralFault,
};

/** @brief Guest memory access width; unsupported device widths are rejected. */
enum class Width : uint8_t { kByte = 1, kHalfWord = 2, kWord = 4 };

template <typename T>
using Expected = std::expected<T, BusError>;

namespace intr {
using intr_n_t = uint32_t; ///< External NVIC interrupt index, not an x86 vector.
}

} // namespace micro_forge::device
