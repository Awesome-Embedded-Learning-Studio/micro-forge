/**
 * @file engine.cpp
 * @brief Engine facade bodies (engine.hpp).
 * @version 0.1.0
 */

#include "micro_forge/engine.hpp"

namespace micro_forge {

bool Engine::load_image(uint64_t guest_addr, std::span<const uint8_t> image) {
    return machine_.load_image(guest_addr, image);
}

arch::x86_64::ExitStatus Engine::run(uint64_t max_instructions) {
    return machine_.run(max_instructions);
}

std::optional<kernel::SnapshotError> Engine::restore(std::span<const uint8_t> blob) {
    return machine_.restore(blob);
}

} // namespace micro_forge
