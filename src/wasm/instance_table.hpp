/**
 * @file instance_table.hpp
 * @brief The wasm ABI's multi-instance machine table.
 * @version 0.1.0
 */

#pragma once

#include <cstdint>

#include "micro_forge/machine.hpp"

namespace micro_forge::wasm {

/// @brief Creates the default instance on first use (the legacy ABI's machine).
/// @return The machine the legacy exports act on.
Machine& current();

/// @brief Creates a machine instance over the guest window.
/// @param[in] ram_size Usable guest RAM in bytes (0 selects the default).
/// @param[in] window Host window base (0 selects the default window).
/// @return A handle >= 1, or 0 when allocation failed.
uint32_t instance_create(uint32_t ram_size, uint32_t window);

/// @brief Destroys an instance; the default instance is never destroyed.
/// @param[in] handle Handle from instance_create.
void instance_destroy(uint32_t handle);

/// @brief Selects the instance the legacy exports act on.
/// @param[in] handle Handle from instance_create (0 = default).
/// @return 1 on success, 0 for an unknown handle.
uint32_t instance_use(uint32_t handle);

} // namespace micro_forge::wasm
