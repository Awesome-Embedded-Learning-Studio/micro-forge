/**
 * @file bus.hpp
 * @brief Address-routing bus shared by all machine lines.
 * @version 0.1.0
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

#include "micro_forge/kernel/bus/bus_error.hpp"
#include "micro_forge/kernel/memory/region.hpp"

namespace micro_forge::kernel {

class RamRegion;

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
     * @brief Maps ordinary RAM and opts into direct read/write access.
     *
     * Use map() for regions with access callbacks, including RamRegion
     * subclasses overriding width accesses. Backing must remain stable.
     * @param[in] base Guest physical base.
     * @param[in] size Mapping length.
     * @param[in] ram Side-effect-free RAM backing.
     * @return Mapping result, with the same errors as map().
     */
    [[nodiscard]] std::expected<void, BusError> map_ram(uint64_t base, uint64_t size,
                                                        RamRegion& ram);

    /**
     * @brief Reads one byte of guest memory.
     *
     * @param[in] address Absolute guest address.
     * @return The byte, or a BusError carrying the faulting address.
     */
    [[nodiscard]] std::expected<uint8_t, BusError> read_u8(uint64_t address);

    /// @brief Fast path for instruction fetch: a contiguous view of the
    ///        bytes at @p address, or an empty span when the mapping does
    ///        not offer one (devices, out-of-bounds).
    ///
    /// @param[in] address Guest physical address.
    /// @param[in] len Bytes wanted.
    /// @return The view, or an empty span.
    [[nodiscard]] std::span<const uint8_t> peek(uint64_t address, std::size_t len) const;

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

    /**
     * @brief Resolves a complete access within registered ordinary RAM.
     *
     * @param[in] address Guest physical start.
     * @param[in] width Bytes accessed.
     * @param[out] host Integer host address on success (0 is valid).
     * @return True for RAM; false requires normal width access routing.
     */
    [[nodiscard]] bool direct_ram(uint64_t address, unsigned width, uintptr_t& host) const {
        const uint64_t offset = address - direct_base_;
        if (offset >= direct_size_ || width > direct_size_ - offset) {
            return false;
        }
        host = direct_host_ + static_cast<uintptr_t>(offset);
        return true;
    }

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
    // Last-resolved mapping: sequential accesses (fetch, memset) hit it
    // instead of bisecting the table every time. Cleared by map()/unmap().
    mutable uint64_t cached_base_ = 0;
    mutable uint64_t cached_end_ = 0;
    mutable Region* cached_region_ = nullptr;
    /// Last registered RAM window. Adding non-overlapping mappings keeps
    /// it valid, and MMIO accesses do not evict it.
    uint64_t direct_base_ = 0;
    uint64_t direct_size_ = 0;
    uintptr_t direct_host_ = 0;
};

} // namespace micro_forge::kernel
