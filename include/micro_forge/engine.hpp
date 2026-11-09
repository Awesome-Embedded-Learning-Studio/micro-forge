/**
 * @file engine.hpp
 * @brief The embedding facade: one machine, load, run, observe, snapshot.
 * @version 0.1.0
 *
 * Machine is the assembly and the low-level surface; Engine is the
 * stable entry point an embedder programs against. It owns one machine,
 * forwards the common operations, and keeps the raw Machine reachable
 * (machine()) for advanced use. Nothing here changes engine behavior.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "micro_forge/base/machine_config.hpp"
#include "micro_forge/machine.hpp"

namespace micro_forge {

/**
 * @brief One engine instance: a configured machine plus the usual verbs.
 *
 * Thread-safety: an Engine is single-threaded, like the machine inside.
 */
class Engine {
public:
    /**
     * @brief Builds an engine from a layout description.
     *
     * @param[in] config The machine layout (RAM, windows, devices).
     */
    explicit Engine(const MachineConfig& config) : machine_(config) {}

    /**
     * @brief Builds an engine with the Cinux preset layout.
     *
     * @param[in] ram_size Usable guest RAM in bytes.
     * @param[in] window Host window base (wasm); null for owning mode.
     */
    explicit Engine(uint64_t ram_size, std::optional<uintptr_t> window = std::nullopt)
        : machine_(ram_size, window) {}

    /**
     * @brief Copies an image into guest RAM.
     *
     * @param[in] guest_addr Destination guest-physical address.
     * @param[in] image The bytes to place.
     * @return False when the image does not fit.
     */
    [[nodiscard]] bool load_image(uint64_t guest_addr, std::span<const uint8_t> image) {
        return machine_.load_image(guest_addr, image);
    }

    /**
     * @brief Boots a Cinux kernel image (boot shim) and attaches no disk.
     *
     * For a disk, attach it through `machine()` before calling boot, or
     * use the wasm ABI's boot entry which takes both blobs.
     *
     * @param[in] kernel kernel.bin bytes.
     * @return True when the shim accepted the image.
     */
    [[nodiscard]] bool boot_cinux(std::span<const uint8_t> kernel);

    /**
     * @brief Runs up to @p max_instructions instructions.
     *
     * @param[in] max_instructions Budget for this call.
     * @return The latched status (kRunning when the budget ran out).
     */
    [[nodiscard]] arch::x86_64::ExitStatus run(uint64_t max_instructions) {
        return machine_.run(max_instructions);
    }

    /// @brief Serial console bytes (device sink).
    [[nodiscard]] std::span<const char> console() const { return machine_.console(); }

    /**
     * @brief Queues one byte on the COM1 receiver.
     *
     * @param[in] byte The byte as typed.
     */
    void inject_serial(uint8_t byte) { machine_.inject_serial(byte); }

    /// @brief Full machine snapshot blob.
    [[nodiscard]] std::vector<uint8_t> snapshot() const { return machine_.snapshot(); }

    /**
     * @brief Restores from a snapshot blob.
     *
     * @param[in] blob Bytes produced by snapshot().
     * @return Empty on success, the refusal reason otherwise.
     */
    [[nodiscard]] std::optional<kernel::SnapshotError> restore(std::span<const uint8_t> blob) {
        return machine_.restore(blob);
    }

    /// @brief The underlying machine (advanced use: devices, CPU, bus).
    [[nodiscard]] Machine& machine() { return machine_; }
    /// @brief The underlying machine, const overload.
    [[nodiscard]] const Machine& machine() const { return machine_; }

private:
    Machine machine_;
};

} // namespace micro_forge
