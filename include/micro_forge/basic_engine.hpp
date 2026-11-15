/**
 * @file basic_engine.hpp
 * @brief Statically dispatched embedding facade over a concrete machine model.
 * @version 0.1.0
 *
 * No CPU, device catalogue or contiguous RAM layout is required here. Machine
 * construction happens at its final address because internal references and
 * callbacks can point at its members. Optional services remain optional.
 */

#pragma once

#include <concepts>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
#include <vector>

#include "micro_forge/machine_model.hpp"

namespace micro_forge {

/** @brief Single-threaded owner of one machine satisfying MachineModel. */
template <MachineModel Model>
class BasicEngine {
public:
    /**
     * @brief Constructs the concrete machine directly in its final storage.
     * @param[in] tag Selects in-place construction without copying a machine.
     * @param[in] args Arguments for the machine's own constructor.
     */
    template <typename... Args>
        requires std::constructible_from<Model, Args...>
    explicit BasicEngine(std::in_place_t /*tag*/, Args&&... args)
        : machine_(std::forward<Args>(args)...) {}

    BasicEngine(const BasicEngine&) = delete;
    BasicEngine& operator=(const BasicEngine&) = delete;
    BasicEngine(BasicEngine&&) = delete;
    BasicEngine& operator=(BasicEngine&&) = delete;

    /**
     * @brief Places image bytes according to the concrete machine's memory map.
     * @param[in] address Guest physical destination, including Flash if supported.
     * @param[in] image Bytes for the loader to place.
     * @return False if the machine rejects the destination or image extent.
     */
    [[nodiscard]] bool load_image(uint64_t address, std::span<const uint8_t> image) {
        return machine_.load_image(address, image);
    }

    /**
     * @brief Runs with an instruction budget using the machine's execution loop.
     * @param[in] max_instructions Instruction budget requested from the machine.
     * @return Common stop information, with machine-specific diagnostic fields.
     */
    RunResult run(uint64_t max_instructions) { return machine_.run_for(max_instructions); }

    /// @brief Concrete machine access for its reset protocol and device controls.
    [[nodiscard]] Model& machine() noexcept { return machine_; }
    /// @brief Read-only access to the concrete machine.
    [[nodiscard]] const Model& machine() const noexcept { return machine_; }

    /**
     * @brief Reads text output when the machine offers a console.
     * @return Machine-owned output, valid until its backing storage changes.
     */
    [[nodiscard]] std::span<const char> console() const
        requires ConsoleMachine<Model>
    {
        return machine_.console();
    }

    /**
     * @brief Queues input when the machine offers a serial receiver.
     * @param[in] byte Input byte for that receiver.
     */
    void inject_serial(uint8_t byte)
        requires SerialInputMachine<Model>
    {
        machine_.inject_serial(byte);
    }

    /**
     * @brief Saves a machine that provides snapshot support.
     * @return Machine-specific state in its snapshot envelope.
     */
    [[nodiscard]] std::vector<uint8_t> snapshot() const
        requires SnapshotMachine<Model>
    {
        return machine_.snapshot();
    }

    /**
     * @brief Restores a snapshot using the concrete machine's validation rules.
     * @param[in] blob Previously saved machine state.
     * @return Success, or the machine's snapshot refusal reason.
     */
    [[nodiscard]] std::expected<void, kernel::SnapshotError> restore(std::span<const uint8_t> blob)
        requires SnapshotMachine<Model>
    {
        return machine_.restore(blob);
    }

private:
    Model machine_;
};

} // namespace micro_forge
