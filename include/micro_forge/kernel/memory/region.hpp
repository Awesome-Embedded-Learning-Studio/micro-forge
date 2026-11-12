/**
 * @file region.hpp
 * @brief Region interface: one span of guest address space.
 * @version 0.1.0
 */

#pragma once

#include <cstdint>
#include <expected>
#include <span>

#include "micro_forge/kernel/bus/bus_error.hpp"

namespace micro_forge::kernel {

/**
 * @brief A contiguous span of guest address space served by one
 * implementation (RAM, a peripheral, a ROM loader target, ...).
 *
 * Regions are address-agnostic: they only ever see offsets relative to
 * their mapping base. Every width is pure virtual on purpose: access
 * semantics differ per width on real hardware (side effects, register
 * banks), so each implementation states its own behavior for all six
 * operations instead of inheriting a byte-composed default that is
 * silently wrong for register-style peripherals.
 */
class Region {
public:
    virtual ~Region() = default;

    /**
     * @brief Reads one byte at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @return The byte, or a BusError whose address is region-local.
     */
    /// @brief Contiguous byte view for fast paths (instruction fetch).
    ///
    /// Default: not offered (devices, register windows). RamRegion
    /// overrides this with its storage pointer.
    ///
    /// @param[in] offset Byte offset relative to the mapping base.
    /// @param[in] len Number of bytes wanted.
    /// @return The view, or an empty span when unavailable.
    [[nodiscard]] virtual std::span<const uint8_t> peek(uint64_t offset, std::size_t len) const {
        (void)offset;
        (void)len;
        return {};
    }

    [[nodiscard]] virtual std::expected<uint8_t, BusError> read_u8(uint64_t offset) = 0;

    /**
     * @brief Reads a little-endian 16-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @return The value, or a BusError whose address is region-local.
     */
    [[nodiscard]] virtual std::expected<uint16_t, BusError> read_u16(uint64_t offset) = 0;

    /**
     * @brief Reads a little-endian 32-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @return The value, or a BusError whose address is region-local.
     */
    [[nodiscard]] virtual std::expected<uint32_t, BusError> read_u32(uint64_t offset) = 0;

    /**
     * @brief Reads a little-endian 64-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @return The value, or a BusError whose address is region-local.
     */
    [[nodiscard]] virtual std::expected<uint64_t, BusError> read_u64(uint64_t offset) = 0;

    /**
     * @brief Writes one byte at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @param[in] value Byte to store.
     * @return Success, or a BusError whose address is region-local.
     */
    [[nodiscard]] virtual std::expected<void, BusError> write_u8(uint64_t offset,
                                                                 uint8_t value) = 0;

    /**
     * @brief Writes a little-endian 16-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @param[in] value Value to store.
     * @return Success, or a BusError whose address is region-local.
     */
    [[nodiscard]] virtual std::expected<void, BusError> write_u16(uint64_t offset,
                                                                  uint16_t value) = 0;

    /**
     * @brief Writes a little-endian 32-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @param[in] value Value to store.
     * @return Success, or a BusError whose address is region-local.
     */
    [[nodiscard]] virtual std::expected<void, BusError> write_u32(uint64_t offset,
                                                                  uint32_t value) = 0;

    /**
     * @brief Writes a little-endian 64-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @param[in] value Value to store.
     * @return Success, or a BusError whose address is region-local.
     */
    [[nodiscard]] virtual std::expected<void, BusError> write_u64(uint64_t offset,
                                                                  uint64_t value) = 0;
};

} // namespace micro_forge::kernel
