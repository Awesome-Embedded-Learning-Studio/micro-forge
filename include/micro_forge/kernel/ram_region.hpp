/**
 * @file ram_region.hpp
 * @brief Plain RAM backed by a byte vector.
 * @version 0.1.0
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

#include "micro_forge/kernel/region.hpp"

namespace micro_forge::kernel {

/**
 * @brief Plain zero-initialized RAM behind a region mapping.
 *
 * Serves FLASH and SRAM spans alike; loaders write program images
 * through data().
 */
class RamRegion : public Region {
public:
    /**
     * @brief Constructs a zero-filled region of @p size bytes.
     *
     * @param[in] size Region size in bytes.
     */
    explicit RamRegion(uint64_t size) : bytes_(size) {}

    /**
     * @brief Reads one byte at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @return The byte, or kOutOfBounds when @p offset is past the end.
     */
    [[nodiscard]] std::expected<uint8_t, BusError> read_u8(uint64_t offset) override;

    /**
     * @brief Reads a little-endian 16-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @return The value, or kOutOfBounds when the access crosses the end.
     */
    [[nodiscard]] std::expected<uint16_t, BusError> read_u16(uint64_t offset) override;

    /**
     * @brief Reads a little-endian 32-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @return The value, or kOutOfBounds when the access crosses the end.
     */
    [[nodiscard]] std::expected<uint32_t, BusError> read_u32(uint64_t offset) override;

    /**
     * @brief Writes one byte at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @param[in] value Byte to store.
     * @return Success, or kOutOfBounds when @p offset is past the end.
     */
    [[nodiscard]] std::expected<void, BusError> write_u8(uint64_t offset, uint8_t value) override;

    /**
     * @brief Writes a little-endian 16-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @param[in] value Value to store.
     * @return Success, or kOutOfBounds when the access crosses the end.
     */
    [[nodiscard]] std::expected<void, BusError> write_u16(uint64_t offset, uint16_t value) override;

    /**
     * @brief Writes a little-endian 32-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @param[in] value Value to store.
     * @return Success, or kOutOfBounds when the access crosses the end.
     */
    [[nodiscard]] std::expected<void, BusError> write_u32(uint64_t offset, uint32_t value) override;

    /// @brief Region size in bytes.
    [[nodiscard]] uint64_t size() const { return bytes_.size(); }

    /**
     * @brief Raw byte access for loaders and snapshot code.
     *
     * Not routed through the bus; callers own bounds checking.
     *
     * @return Pointer to the first byte of the backing storage.
     */
    [[nodiscard]] uint8_t* data() { return bytes_.data(); }

private:
    /// Returns the region-local address of the first out-of-bounds byte for
    /// [offset, offset + width), or nullopt when the access fits.
    [[nodiscard]] std::optional<uint64_t> first_out_of_bounds(uint64_t offset,
                                                              uint64_t width) const;

    std::vector<uint8_t> bytes_;
};

} // namespace micro_forge::kernel
