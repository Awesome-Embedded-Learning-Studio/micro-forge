/**
 * @file bus.hpp
 * @brief Address-routing bus shared by all machine lines.
 * @version 0.1.0
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

#include "micro_forge/kernel/bus_error.hpp"
#include "micro_forge/kernel/region.hpp"

namespace micro_forge::kernel {

/**
 * @brief Routes guest addresses to mapped regions.
 *
 * This is the shared machine kernel: both machine lines (Cortex-M3,
 * x86_64) sit on the same bus shape. Region lifetime is owned by the
 * caller (the SoC factory); the bus holds non-owning pointers.
 */
class Bus {
public:
    /**
     * @brief Registers [base, base + size) to be served by @p region.
     *
     * Adjacent mappings are allowed.
     *
     * @param[in] base Base guest address of the mapping.
     * @param[in] size Mapping size in bytes; must not be zero or wrap.
     * @param[in] region Region serving the mapping; caller owns it.
     * @return Success, or kInvalidMapping on zero size or address
     *         wrap-around, kOverlap when the range intersects an
     *         existing mapping.
     */
    [[nodiscard]] std::expected<void, BusError> map(uint64_t base, uint64_t size, Region& region);

    /**
     * @brief Reads one byte of guest memory.
     *
     * @param[in] address Absolute guest address.
     * @return The byte, or a BusError carrying the faulting address.
     */
    [[nodiscard]] std::expected<uint8_t, BusError> read_u8(uint64_t address);

    /**
     * @brief Reads a little-endian 16-bit value of guest memory.
     *
     * @param[in] address Absolute guest address.
     * @return The value, or a BusError carrying the faulting address.
     */
    [[nodiscard]] std::expected<uint16_t, BusError> read_u16(uint64_t address);

    /**
     * @brief Reads a little-endian 32-bit value of guest memory.
     *
     * @param[in] address Absolute guest address.
     * @return The value, or a BusError carrying the faulting address.
     */
    [[nodiscard]] std::expected<uint32_t, BusError> read_u32(uint64_t address);

    /**
     * @brief Reads a little-endian 64-bit value of guest memory.
     *
     * @param[in] address Absolute guest address.
     * @return The value, or a BusError carrying the faulting address.
     */
    [[nodiscard]] std::expected<uint64_t, BusError> read_u64(uint64_t address);

    /**
     * @brief Writes one byte of guest memory.
     *
     * @param[in] address Absolute guest address.
     * @param[in] value Byte to store.
     * @return Success, or a BusError carrying the faulting address.
     */
    [[nodiscard]] std::expected<void, BusError> write_u8(uint64_t address, uint8_t value);

    /**
     * @brief Writes a little-endian 16-bit value of guest memory.
     *
     * @param[in] address Absolute guest address.
     * @param[in] value Value to store.
     * @return Success, or a BusError carrying the faulting address.
     */
    [[nodiscard]] std::expected<void, BusError> write_u16(uint64_t address, uint16_t value);

    /**
     * @brief Writes a little-endian 32-bit value of guest memory.
     *
     * @param[in] address Absolute guest address.
     * @param[in] value Value to store.
     * @return Success, or a BusError carrying the faulting address.
     */
    [[nodiscard]] std::expected<void, BusError> write_u32(uint64_t address, uint32_t value);

    /**
     * @brief Writes a little-endian 64-bit value of guest memory.
     *
     * @param[in] address Absolute guest address.
     * @param[in] value Value to store.
     * @return Success, or a BusError carrying the faulting address.
     */
    [[nodiscard]] std::expected<void, BusError> write_u64(uint64_t address, uint64_t value);

private:
    struct Mapping {
        uint64_t base;
        uint64_t end; // one past the last address
        Region* region;
    };

    struct Resolved {
        Region* region;
        uint64_t base; // mapping base, to absolutize region errors
        uint64_t offset;
    };

    /// Returns nullopt when no mapping covers the address.
    [[nodiscard]] std::optional<Resolved> resolve(uint64_t address) const;

    std::vector<Mapping> mappings_; // sorted by base, non-overlapping
};

} // namespace micro_forge::kernel
