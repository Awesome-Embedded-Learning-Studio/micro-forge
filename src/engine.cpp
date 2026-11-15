/**
 * @file engine.cpp
 * @brief Engine facade bodies (engine.hpp).
 * @version 0.1.0
 */

#include "micro_forge/engine.hpp"

#include <cstdint>

#include "micro_forge/arch/x86_64/cpu.hpp"

namespace micro_forge {

arch::x86_64::ExitStatus Engine::run(uint64_t max_instructions) {
    return machine().run(max_instructions);
}

} // namespace micro_forge
