/**
 * @file machine.hpp
 * @brief Source-compatible name for the x86 PC machine assembly.
 * @version 0.1.0
 *
 * New machine implementations belong in machines/ and own their wiring.
 * This alias preserves existing Cinux and wasm callers without making
 * the PC device set a requirement for other machine models.
 */

#pragma once

#include "micro_forge/base/machine_config.hpp"
#include "micro_forge/machines/x86_pc.hpp"

namespace micro_forge {

/// @brief Historical name for the x86 PC assembly, not a universal machine.
using Machine = machines::X86PcMachine;

} // namespace micro_forge
