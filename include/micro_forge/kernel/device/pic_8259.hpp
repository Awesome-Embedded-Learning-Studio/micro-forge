/**
 * @file pic_8259.hpp
 * @brief 8259A programmable interrupt controller, register model (6b).
 * @version 0.1.0
 *
 * Batch 6b models the register state machine only: the boot path
 * remaps the pair (ICW1-4) and toggles the interrupt mask (OCW1). No
 * interrupt delivery happens here — the IDT injection pipeline owns
 * that (batch 6c). One instance serves one chip; the machine creates
 * the master at 0x20 and the slave at 0xA0.
 */

#pragma once

#include <cstdint>

#include "micro_forge/kernel/bus/port_space.hpp"

namespace micro_forge::kernel {

/**
 * @brief One 8259A chip: command and data ports.
 */
class Pic8259 : public PortDevice {
public:
    /**
     * @brief Answers one chip access (see PortDevice::read).
     * @param[in] offset 0 = command, 1 = data (IMR).
     * @param[in] width Access width; only 1 is served.
     * @param[out] out The register value on success.
     * @return False for unsupported widths or offsets.
     */
    bool read(uint16_t offset, unsigned width, uint64_t& out) override;

    /**
     * @brief Answers one chip access (see PortDevice::write).
     * @param[in] offset 0 = command, 1 = data (IMR).
     * @param[in] width Access width; only 1 is served.
     * @param[in] value The value written.
     * @return False for unsupported widths or offsets.
     */
    bool write(uint16_t offset, unsigned width, uint64_t value) override;

    /// @brief Interrupt vector base configured by ICW2.
    [[nodiscard]] uint8_t vector_base() const { return vector_base_; }
    /// @brief Current interrupt mask register.
    [[nodiscard]] uint8_t mask() const { return mask_; }
    /// @brief Whether the init sequence (ICW1) has been accepted.
    [[nodiscard]] bool initialized() const { return initialized_; }

private:
    uint8_t vector_base_ = 0;
    uint8_t mask_ = 0xFF; // all masked out of reset
    uint8_t icw3_ = 0;
    uint8_t icw4_ = 0;
    unsigned icw_pos_ = 0; // 0 = idle, 1..3 = waiting for ICW2..ICW4
    bool initialized_ = false;
};

} // namespace micro_forge::kernel
