/**
 * @file cinux_machine.hpp
 * @brief The Cinux preset: the historical hard-coded layout, as data.
 * @version 0.1.0
 */

#pragma once

#include <cstdint>
#include <optional>

#include "micro_forge/base/machine_config.hpp"

namespace micro_forge {

/**
 * @brief The preset that reproduces Cinux's historical layout.
 *
 * COM1 0x3F8, PIT 0x40, board 0x61, RTC 0x70, POST 0x80, fw_cfg 0x510,
 * PCI 0xCF8, keyboard 0x60/0x64, PICs 0x20/0xA0, AHCI BAR 0xFE000000,
 * LAPIC 0xFEE00000, IO-APIC 0xFEC00000.
 *
 * @param[in] ram_size Usable guest RAM in bytes.
 * @param[in] window Host window base for wasm; null for owning mode.
 * @return The preset configuration.
 */
[[nodiscard]] MachineConfig cinux_machine_config(uint64_t ram_size,
                                                 std::optional<uintptr_t> window = std::nullopt);

} // namespace micro_forge
