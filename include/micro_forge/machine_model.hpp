/**
 * @file machine_model.hpp
 * @brief Compile-time requirements for machine execution and optional services.
 * @version 0.1.0
 *
 * A machine owns its CPU, memory map, reset protocol and device wiring.
 * These requirements describe caller-visible operations, not its components.
 * Time accounting and interrupt delivery remain machine responsibilities.
 */

#pragma once

#include <concepts>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "micro_forge/base/run_result.hpp"
#include "micro_forge/kernel/snapshot.hpp"

namespace micro_forge {

/**
 * @brief A machine that accepts physical images and a retired-instruction budget.
 *
 * load_image is an explicit loader operation, including for ROM or Flash
 * backing, not a guest bus write. The machine validates its own destinations.
 * run_for accepts a retired-instruction budget. Instruction accounting and
 * the exact stop boundary belong to the concrete execution loop, not this
 * compile-time check. Reset and boot state use the concrete machine API.
 */
template <typename Model>
concept MachineModel =
    requires(Model& machine, uint64_t address, uint64_t budget, std::span<const uint8_t> image) {
        { machine.load_image(address, image) } -> std::same_as<bool>;
        { machine.run_for(budget) } -> std::same_as<RunResult>;
    };

/** @brief Optional text output, independent of the device producing it. */
template <typename Model>
concept ConsoleMachine = requires(const Model& machine) {
    { machine.console() } -> std::same_as<std::span<const char>>;
};

/** @brief Optional serial input accepted by a machine-owned receiver. */
template <typename Model>
concept SerialInputMachine = requires(Model& machine, uint8_t byte) {
    { machine.inject_serial(byte) } -> std::same_as<void>;
};

/** @brief Optional save/restore support using the shared snapshot error channel. */
template <typename Model>
concept SnapshotMachine =
    requires(Model& machine, const Model& observed, std::span<const uint8_t> blob) {
        { observed.snapshot() } -> std::same_as<std::vector<uint8_t>>;
        { machine.restore(blob) } -> std::same_as<std::expected<void, kernel::SnapshotError>>;
    };

} // namespace micro_forge
