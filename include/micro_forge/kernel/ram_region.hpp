/**
 * @file ram_region.hpp
 * @brief Plain RAM backed by a byte vector.
 * @version 0.1.0
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

#include "micro_forge/kernel/region.hpp"
#include "micro_forge/kernel/snapshot.hpp"

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
    explicit RamRegion(uint64_t size)
        : bytes_(size), base_(reinterpret_cast<uintptr_t>(bytes_.data())), size_(size) {}

    /// @brief Tag for window construction: backing addressed by integer
    ///        base (wasm linear offsets; zero is the guest window).
    struct WindowBase {
        /// Backing base as an address-sized integer.
        uintptr_t value;
    };

    /**
     * @brief Non-owning window mode: the region backs memory the caller
     *        owns (batch 5: the wasm linear memory, where guest physical
     *        0 equals offset 0 by the spec 5.4 rule).
     *
     * @param[in] base Backing base as an integer.
     * @param[in] size Window size in bytes.
     */
    RamRegion(WindowBase base, uint64_t size) : base_(base.value), size_(size) {}

    /**
     * @brief Reads one byte at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @return The byte, or kOutOfBounds when @p offset is past the end.
     */
    /// @brief Contiguous view of the backing store (peek fast path).
    /// @param[in] offset Byte offset into the region.
    /// @param[in] len Bytes wanted.
    /// @return The view, or empty when out of bounds.
    [[nodiscard]] std::span<const uint8_t> peek(uint64_t offset, std::size_t len) const override;

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
     * @brief Reads a little-endian 64-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @return The value, or kOutOfBounds when the access crosses the end.
     */
    [[nodiscard]] std::expected<uint64_t, BusError> read_u64(uint64_t offset) override;

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

    /**
     * @brief Writes a little-endian 64-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @param[in] value Value to store.
     * @return Success, or kOutOfBounds when the access crosses the end.
     */
    [[nodiscard]] std::expected<void, BusError> write_u64(uint64_t offset, uint64_t value) override;

    /// @brief Region size in bytes.
    [[nodiscard]] uint64_t size() const { return size_; }

    /// @brief Integer backing base for explicit direct RAM mappings.
    /// @return Byte address, including valid wasm linear offset 0.
    [[nodiscard]] uintptr_t backing_base() const { return base_; }

    /**
     * @brief Serializes size and contents into @p writer.
     *
     * @param[in,out] writer Snapshot payload writer.
     */
    void save(SnapshotWriter& writer) const;

    /**
     * @brief Replaces contents from @p reader; the size must match the
     *        constructed region (mapping layout is config, not state).
     *
     * @param[in,out] reader Positioned snapshot reader.
     * @return Success; false on short or mismatched data.
     */
    [[nodiscard]] bool restore(SnapshotReader& reader);

    /**
     * @brief Bulk copy into the region (loader path, window-safe).
     *
     * @param[in] offset Region-local destination offset.
     * @param[in] src Bytes to place; must fit from @p offset.
     * @return Success; false when the span leaves the region.
     */
    [[nodiscard]] bool copy_in(uint64_t offset, std::span<const uint8_t> src);

    /**
     * @brief Read-only view into the region (window-safe, like copy_in).
     *
     * @param[in] offset Region-local start.
     * @param[in] len Byte count; must fit from @p offset.
     * @return The view, or an empty span when the range leaves the region.
     */
    [[nodiscard]] std::span<const uint8_t> view(uint64_t offset, uint64_t len) const;

    /**
     * @brief Raw byte access. Owning mode only: in window mode the base
     *        is linear-memory offset 0, and pointer arithmetic through a
     *        null constant is optimizer poison -- window consumers use
     *        copy_in and the width accesses instead.
     *
     * @return Pointer to the first byte of the backing storage.
     */
    [[nodiscard]] uint8_t* data() { return bytes_.data(); }

private:
    /// Returns the region-local address of the first out-of-bounds byte for
    /// [offset, offset + width), or nullopt when the access fits.
    [[nodiscard]] std::optional<uint64_t> first_out_of_bounds(uint64_t offset,
                                                              uint64_t width) const;

    /// Integer-base pointer for one access (see base_ for the rationale).
    [[nodiscard]] uint8_t* ptr_at(uint64_t offset) const;

    /// Owning storage; empty in window mode.
    std::vector<uint8_t> bytes_;
    /// Backing as an integer base: pointer arithmetic on a null constant
    /// is poison to the optimizer, while uintptr + offset -> pointer is
    /// the honest wasm32 pattern (offsets are pointers).
    uintptr_t base_ = 0;
    /// Size in bytes.
    uint64_t size_;
};

} // namespace micro_forge::kernel
