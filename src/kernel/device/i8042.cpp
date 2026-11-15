/**
 * @file i8042.cpp
 * @brief 8042 controller port bodies (i8042.hpp).
 * @version 0.1.0
 */

#include "micro_forge/kernel/device/i8042.hpp"

namespace micro_forge::kernel {

bool I8042::read(uint16_t /*offset*/, unsigned width, uint64_t& out) {
    if (width != 1) {
        return false;
    }
    if (port_ == kStatus) {
        // OBF (bit 0) when data waits, IBF (bit 1) never blocks.
        out = state_.output.empty() ? 0U : 1U;
        return true;
    }
    if (state_.output.empty()) {
        out = 0; // no data buffered: reads return zero
        return true;
    }
    out = state_.output.front();
    state_.output.pop_front();
    return true;
}

bool I8042::write(uint16_t /*offset*/, unsigned width, uint64_t value) {
    if (width != 1) {
        return false;
    }
    const auto byte = static_cast<uint8_t>(value);
    if (port_ == kData) {
        state_.config = byte; // only reached after the write-config command
        return true;
    }
    switch (byte) {
    case 0x20: // read configuration byte
        state_.output.push_back(state_.config);
        break;
    case 0xAA: // controller self-test
        state_.output.push_back(0x55);
        break;
    default: // 0xAD/0xA7/0xAE/0x60 and friends: accepted as writes
        break;
    }
    return true;
}

} // namespace micro_forge::kernel
