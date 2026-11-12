/**
 * @file register_region.hpp
 * @brief MMIO register window base: six-width access over a byte array.
 * @version 0.1.0
 *
 * Closes the batch-1 open item (RegisterRegionBase): devices whose state
 * is a flat register file behind a memory-mapped window get the width
 * plumbing here and override only the registers with behavior (EOI
 * clears, self-clearing bits, counters). The template method keeps the
 * bus-facing six widths in one place.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "micro_forge/kernel/bus/bus.hpp"

namespace micro_forge::kernel {

/**
 * @brief A byte-array MMIO window with width-aware access.
 */
class RegisterRegionBase : public Region {
public:
    /**
     * @brief Binds the backing store.
     *
     * @param[in] window Caller-owned bytes; must outlive the region.
     * @param[in] size Window size in bytes (power-of-two sizes preferred).
     */
    RegisterRegionBase(uint8_t* window, std::size_t size) : window_(window), size_(size) {}

    /// @brief Reads one byte from the window (see Region::read_u8).
    /// @param[in] offset Window offset.
    /// @return The byte, or kOutOfBounds past the window.
    std::expected<uint8_t, BusError> read_u8(uint64_t offset) override;
    /// @brief Reads little-endian 16 bits (see Region::read_u16).
    /// @param[in] offset Window offset.
    /// @return The value, or kOutOfBounds past the window.
    std::expected<uint16_t, BusError> read_u16(uint64_t offset) override;
    /// @brief Reads little-endian 32 bits (see Region::read_u32).
    /// @param[in] offset Window offset.
    /// @return The value, or kOutOfBounds past the window.
    std::expected<uint32_t, BusError> read_u32(uint64_t offset) override;
    /// @brief Reads little-endian 64 bits (see Region::read_u64).
    /// @param[in] offset Window offset.
    /// @return The value, or kOutOfBounds past the window.
    std::expected<uint64_t, BusError> read_u64(uint64_t offset) override;
    /// @brief Writes one byte (see Region::write_u8).
    /// @param[in] offset Window offset.
    /// @param[in] value The byte written.
    /// @return Success, or kOutOfBounds past the window.
    std::expected<void, BusError> write_u8(uint64_t offset, uint8_t value) override;
    /// @brief Writes little-endian 16 bits (see Region::write_u16).
    /// @param[in] offset Window offset.
    /// @param[in] value The value written.
    /// @return Success, or kOutOfBounds past the window.
    std::expected<void, BusError> write_u16(uint64_t offset, uint16_t value) override;
    /// @brief Writes little-endian 32 bits (see Region::write_u32).
    /// @param[in] offset Window offset.
    /// @param[in] value The value written.
    /// @return Success, or kOutOfBounds past the window.
    std::expected<void, BusError> write_u32(uint64_t offset, uint32_t value) override;
    /// @brief Writes little-endian 64 bits (see Region::write_u64).
    /// @param[in] offset Window offset.
    /// @param[in] value The value written.
    /// @return Success, or kOutOfBounds past the window.
    std::expected<void, BusError> write_u64(uint64_t offset, uint64_t value) override;

protected:
    /// Raw window access; subclasses override the six-width hooks below.
    [[nodiscard]] uint8_t* window() { return window_; }
    /// Const window access (read-only register inspection).
    [[nodiscard]] const uint8_t* window() const { return window_; }
    /// Window size in bytes.
    [[nodiscard]] std::size_t window_size() const { return size_; }

    /**
     * @brief Behavior hook before a read is served (e.g. counters).
     *
     * @param[in] offset Access offset inside the window.
     * @param[in] width Access width in bytes.
     */
    virtual void on_read(uint64_t offset, unsigned width) {
        (void)offset;
        (void)width;
    }
    /**
     * @brief Behavior hook before a write lands (e.g. EOI clears).
     *
     * @param[in] offset Access offset inside the window.
     * @param[in] width Access width in bytes.
     * @param[in] value The value written.
     */
    virtual void on_write(uint64_t offset, unsigned width, uint64_t value) {
        (void)offset;
        (void)width;
        (void)value;
    }

private:
    uint8_t* window_;
    std::size_t size_;
};

} // namespace micro_forge::kernel
