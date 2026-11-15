/**
 * @file fw_cfg.cpp
 * @brief fw_cfg port bodies (fw_cfg.hpp).
 * @version 0.1.0
 */

#include "micro_forge/kernel/device/fw_cfg.hpp"

namespace micro_forge::kernel {

bool FwCfg::read(uint16_t offset, unsigned width, uint64_t& out) {
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

bool FwCfg::write(uint16_t offset, unsigned width, uint64_t value) {
    if (width == 0 || width > 4) {
        return false;
    }
    if (offset == 0) {
        selector_ = static_cast<uint16_t>(value);
        return true;
    }
    return offset == 1;
}

} // namespace micro_forge::kernel
