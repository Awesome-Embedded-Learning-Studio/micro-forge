/**
 * @file irq_line.hpp
 * @brief Level-triggered interrupt line: the device-to-CPU channel.
 * @version 0.1.0
 */

#pragma once

#include <cstdint>

namespace micro_forge::kernel {

/**
 * @brief One level-triggered interrupt line.
 *
 * This is the whole batch-4 interrupt story on purpose: a line a device
 * raises and lowers, and a CPU observes after retiring instructions.
 * Vector injection (IDT walk, frame push, iret return) is batch-6
 * machinery; until then the receiving side is an attachable hook, so
 * the channel itself is real, deterministic, and snapshot-friendly.
 *
 * Level semantics: the line stays pending until the receiver lowers it
 * (acknowledgement belongs to the receiver, not to the line).
 */
class IrqLine {
public:
    /// @brief Drives the line high. Idempotent.
    void raise() { level_ = true; }

    /// @brief Drives the line low (the receiver's acknowledgement). Idempotent.
    void lower() { level_ = false; }

    /// @brief Whether the line is high.
    [[nodiscard]] bool pending() const { return level_; }

private:
    bool level_ = false;
};

} // namespace micro_forge::kernel
