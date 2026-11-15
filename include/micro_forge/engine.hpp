/**
 * @file engine.hpp
 * @brief Compatibility facade for callers using the original x86 Engine API.
 * @version 0.1.0
 *
 * BasicEngine owns the common implementation. This facade keeps the original
 * config constructor and native x86 run result for existing source callers.
 * New code can use BasicEngine<machines::X86PcMachine> for portable stop reasons.
 */

#pragma once

#include <cstdint>
#include <utility>

#include "micro_forge/base/machine_config.hpp"
#include "micro_forge/basic_engine.hpp"
#include "micro_forge/machine.hpp"

namespace micro_forge {

/** @brief Historical x86 facade with source-compatible construction and run. */
class Engine : public BasicEngine<machines::X86PcMachine> {
public:
    /**
     * @brief Constructs a PC using the original configuration entry point.
     * @param[in] config The PC memory and device layout.
     */
    explicit Engine(const MachineConfig& config) : BasicEngine(std::in_place, config) {}

    /**
     * @brief Runs the PC and preserves the original x86 result type.
     * @param[in] max_instructions Instruction budget for this call.
     * @return Native x86 stop information, unchanged from the old Engine API.
     */
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method) -- legacy result type.
    arch::x86_64::ExitStatus run(uint64_t max_instructions);
};

} // namespace micro_forge
