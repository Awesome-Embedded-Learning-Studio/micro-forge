/**
 * @file port_stubs.cpp
 * @brief Port stub bodies (port_stubs.hpp).
 * @version 0.1.0
 */

#include "micro_forge/kernel/bus/port_stubs.hpp"

namespace micro_forge::kernel {

bool PostPort::read(uint16_t /*offset*/, unsigned width, uint64_t& out) {
    if (width != 1) {
        return false;
    }
    out = last_;
    return true;
}

bool PostPort::write(uint16_t /*offset*/, unsigned width, uint64_t value) {
    if (width != 1) {
        return false;
    }
    last_ = static_cast<uint8_t>(value);
    return true;
}

} // namespace micro_forge::kernel
