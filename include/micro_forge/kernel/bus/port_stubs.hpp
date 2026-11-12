/**
 * @file port_stubs.hpp
 * @brief Single-port stubs the boot path pokes (batch 6c).
 * @version 0.1.0
 *
 * Port 0x80 is the legacy POST diagnostic port: writes are discarded
 * by every real chipset. The kernel writes a progress byte there during
 * bring-up; the machine must swallow it or the boot faults on an
 * unmapped port.
 */

#pragma once

#include <cstdint>

#include "micro_forge/kernel/bus/port_space.hpp"

namespace micro_forge::kernel {

/**
 * @brief Write-only diagnostic port (reads return the last byte).
 */
class PostPort : public PortDevice {
public:
    /**
     * @brief Answers one POST-port read (last written byte).
     * @param[in] offset Always 0 (single-port window).
     * @param[in] width Access width; only 1 is served.
     * @param[out] out The last byte written.
     * @return False for unsupported widths.
     */
    bool read(uint16_t offset, unsigned width, uint64_t& out) override {
        (void)offset;
        if (width != 1) {
            return false;
        }
        out = last_;
        return true;
    }

    /**
     * @brief Answers one POST-port write (latched, no side effects).
     * @param[in] offset Always 0 (single-port window).
     * @param[in] width Access width; only 1 is served.
     * @param[in] value The progress byte.
     * @return False for unsupported widths.
     */
    bool write(uint16_t offset, unsigned width, uint64_t value) override {
        (void)offset;
        if (width != 1) {
            return false;
        }
        last_ = static_cast<uint8_t>(value);
        return true;
    }

private:
    uint8_t last_ = 0;
};

} // namespace micro_forge::kernel
