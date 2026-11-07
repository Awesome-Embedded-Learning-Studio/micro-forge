/**
 * @file debug_exit.hpp
 * @brief Memory-mapped debug-exit region: one u32 write ends the run.
 * @version 0.1.0
 */

#pragma once

#include <cstdint>
#include <expected>
#include <functional>

#include "micro_forge/kernel/bus_error.hpp"
#include "micro_forge/kernel/region.hpp"

namespace micro_forge::kernel {

/**
 * @brief isa-debug-exit as a memory-mapped region (batch-3 scope: the
 *        engine has no port-IO channel yet, so the QEMU port dialect
 *        becomes one u32 register).
 *
 * Semantics follow QEMU's isa-debug-exit device: writing value V makes the
 * machine leave with code (V << 1) | 1, so a guest test harness written
 * against the QEMU convention sees identical exit codes here. Only the
 * 32-bit write at offset 0 is meaningful; every other access is an error,
 * because nothing else is behind this address.
 *
 * Suggested mapping base: 0xFEED0000 (between the planned IO-APIC at
 * 0xFEC00000 and Local APIC at 0xFEE00000 windows). The region itself is
 * address-agnostic; clients pick the base.
 */
class DebugExitRegion : public Region {
public:
    /// Offset of the exit register, in bytes from the mapping base.
    static constexpr uint64_t kRegExit = 0x0;

    /**
     * @brief Constructs the region around an exit sink.
     *
     * @param[in] on_exit Called with the QEMU-encoded exit code on every
     *                    write to the exit register. Wire it to the CPU's
     *                    debug-exit request; it must not touch the bus.
     */
    explicit DebugExitRegion(std::function<void(uint32_t)> on_exit)
        : on_exit_(std::move(on_exit)) {}

    /**
     * @brief Reads one byte at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @return Always kOutOfBounds: the exit register is write-only.
     */
    [[nodiscard]] std::expected<uint8_t, BusError> read_u8(uint64_t offset) override {
        return unexpected_out_of_bounds(offset);
    }

    /**
     * @brief Reads a little-endian 16-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @return Always kOutOfBounds: the exit register is write-only.
     */
    [[nodiscard]] std::expected<uint16_t, BusError> read_u16(uint64_t offset) override {
        return unexpected_out_of_bounds(offset);
    }

    /**
     * @brief Reads a little-endian 32-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @return Always kOutOfBounds: the exit register is write-only.
     */
    [[nodiscard]] std::expected<uint32_t, BusError> read_u32(uint64_t offset) override {
        return unexpected_out_of_bounds(offset);
    }

    /**
     * @brief Reads a little-endian 64-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @return Always kOutOfBounds: the exit register is write-only.
     */
    [[nodiscard]] std::expected<uint64_t, BusError> read_u64(uint64_t offset) override {
        return unexpected_out_of_bounds(offset);
    }

    /**
     * @brief Writes one byte at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @param[in] value Byte to store.
     * @return Always kOutOfBounds: exits trigger through the u32 register.
     */
    [[nodiscard]] std::expected<void, BusError> write_u8(uint64_t offset,
                                                         uint8_t /*value*/) override {
        return unexpected_out_of_bounds(offset);
    }

    /**
     * @brief Writes a little-endian 16-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @param[in] value Value to store.
     * @return Always kOutOfBounds: exits trigger through the u32 register.
     */
    [[nodiscard]] std::expected<void, BusError> write_u16(uint64_t offset,
                                                          uint16_t /*value*/) override {
        return unexpected_out_of_bounds(offset);
    }

    /**
     * @brief Writes a little-endian 32-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @param[in] value Exit payload; the sink receives (value << 1) | 1.
     * @return Success on the exit register, kOutOfBounds elsewhere.
     */
    [[nodiscard]] std::expected<void, BusError> write_u32(uint64_t offset,
                                                          uint32_t value) override {
        if (offset == kRegExit) {
            on_exit_((value << 1U) | 1U);
            return {};
        }
        return unexpected_out_of_bounds(offset);
    }

    /**
     * @brief Writes a little-endian 64-bit value at @p offset.
     *
     * @param[in] offset Byte offset relative to the mapping base.
     * @param[in] value Value to store.
     * @return Always kOutOfBounds: exits trigger through the u32 register.
     */
    [[nodiscard]] std::expected<void, BusError> write_u64(uint64_t offset,
                                                          uint64_t /*value*/) override {
        return unexpected_out_of_bounds(offset);
    }

private:
    [[nodiscard]] static std::unexpected<BusError> unexpected_out_of_bounds(uint64_t offset) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = offset});
    }

    std::function<void(uint32_t)> on_exit_;
};

} // namespace micro_forge::kernel
