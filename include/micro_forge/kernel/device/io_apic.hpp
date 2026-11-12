/**
 * @file io_apic.hpp
 * @brief IO-APIC (IMS) register surface (batch 6c).
 * @version 0.1.0
 *
 * Indirect-register device: IOREGSEL (0x00) selects an index, IOWIN
 * (0x10) reads or writes its 32-bit value. The redirection table is
 * modeled as storage; interrupt routing semantics arrive with the
 * vector pipeline wiring in 6d.
 */

#pragma once

#include <array>
#include <cstdint>

#include "micro_forge/kernel/memory/register_region.hpp"

namespace micro_forge::kernel {

/**
 * @brief The IO-APIC MMIO window (select/data pair).
 */
class IoApic : public RegisterRegionBase {
public:
    /// @brief Register indices.
    enum Registers : uint8_t {
        kId = 0x00,        ///< IO-APIC ID.
        kVersion = 0x01,   ///< Version and max redirection entry.
        kRedirBase = 0x10, ///< First redirection table entry (two words each).
    };

    /// @brief Constructs the window (zero-initialized register file).
    IoApic();

    /// @brief Current IOREGSEL index.
    [[nodiscard]] uint32_t selected() const { return selected_; }
    /// @brief One redirection entry (low word, high word).
    [[nodiscard]] std::array<uint32_t, 2> redirection(unsigned entry) const;

    /// @brief Reads route through the selected register.
    std::expected<uint8_t, BusError> read_u8(uint64_t offset) override;
    /// @brief Reads route through the selected register.
    std::expected<uint16_t, BusError> read_u16(uint64_t offset) override;
    /// @brief Reads route through the selected register.
    std::expected<uint32_t, BusError> read_u32(uint64_t offset) override;
    /// @brief Writes route through the selected register.
    std::expected<void, BusError> write_u8(uint64_t offset, uint8_t value) override;
    /// @brief Writes route through the selected register.
    std::expected<void, BusError> write_u16(uint64_t offset, uint16_t value) override;
    /// @brief Writes route through the selected register.
    std::expected<void, BusError> write_u32(uint64_t offset, uint32_t value) override;

private:
    std::array<uint8_t, 0x1000> window_{};
    std::array<uint32_t, 0x40> registers_{}; // index space (max 64 for 24 entries)
    uint32_t selected_ = 0;
};

} // namespace micro_forge::kernel
