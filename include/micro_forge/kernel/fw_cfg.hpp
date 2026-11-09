/**
 * @file fw_cfg.hpp
 * @brief QEMU fw_cfg selector/data ports (batch 6d).
 * @version 0.1.0
 *
 * The kernel reads firmware strings through the QEMU fw_cfg interface
 * (selector 0x510, data 0x511). This machine provides no firmware
 * entries, so every read answers zero -- the first signature probe
 * ("QEMU") fails and the kernel skips the optional string cleanly
 * instead of faulting on an unmapped port.
 */

#pragma once

#include <cstdint>

#include "micro_forge/kernel/port_space.hpp"

namespace micro_forge::kernel {

/**
 * @brief The fw_cfg selector/data port pair.
 */
class FwCfg : public PortDevice {
public:
    /**
     * @brief Answers one port read (selector echo or zero data).
     * @param[in] offset 0 = selector, 1 = data.
     * @param[in] width Access width; only the driver's widths.
     * @param[out] out Selector value or zero.
     * @return False outside the pair.
     */
    bool read(uint16_t offset, unsigned width, uint64_t& out) override {
        if (width == 0 || width > 4) {
            return false;
        }
        if (offset == 0) {
            out = selector_;
            return true;
        }
        if (offset == 1) {
            out = 0; // no firmware entries: signature probe fails cleanly
            return true;
        }
        return false;
    }

    /**
     * @brief Answers one port write (selector latch).
     * @param[in] offset 0 = selector, 1 = data.
     * @param[in] width Access width.
     * @param[in] value The selector written.
     * @return False outside the pair.
     */
    bool write(uint16_t offset, unsigned width, uint64_t value) override {
        if (width == 0 || width > 4) {
            return false;
        }
        if (offset == 0) {
            selector_ = static_cast<uint16_t>(value);
            return true;
        }
        return offset == 1;
    }

private:
    uint16_t selector_ = 0;
};

} // namespace micro_forge::kernel
