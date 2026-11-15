/**
 * @file active_machine.hpp
 * @brief Assembles the MachineConfig selected by MICRO_FORGE_MACHINE.
 * @version 0.1.0
 *
 * The layout itself lives in the machines directory TOML files and is
 * baked into micro_forge/generated/machine_config_generated.hpp as
 * constexpr data; this header owns the one runtime step: copying those
 * tables into the (vector-based) config struct and filling in the
 * caller's RAM and window.
 */

#pragma once

#include <cstdint>
#include <optional>

#include "micro_forge/base/machine_config.hpp"

namespace micro_forge {

/**
 * @brief The described machine's config, with caller overrides applied.
 *
 * @param[in] ram_size Guest RAM in bytes; 0 takes the description's
 *            default.
 * @param[in] window Host window base for wasm; null for owning mode.
 * @return The config for the selected machine.
 */
[[nodiscard]] MachineConfig active_machine_config(
    uint64_t ram_size = 0, std::optional<uintptr_t> window = std::nullopt);

} // namespace micro_forge
