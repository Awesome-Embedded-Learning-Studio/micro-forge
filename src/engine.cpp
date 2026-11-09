/**
 * @file engine.cpp
 * @brief Engine facade: the Cinux boot convenience.
 * @version 0.1.0
 */

#include "micro_forge/engine.hpp"

#include "micro_forge/loader/cinux_loader.hpp"

namespace micro_forge {

bool Engine::boot_cinux(std::span<const uint8_t> kernel) {
    return loader::boot_cinux(machine_.ram(), machine_.cpu(), kernel).has_value();
}

} // namespace micro_forge
