/**
 * @file pic_8259.cpp
 * @brief 8259A register state machine (batch 6b).
 * @version 0.1.0
 */

#include "micro_forge/kernel/pic_8259.hpp"

namespace micro_forge::kernel {

bool Pic8259::read(uint16_t offset, unsigned width, uint64_t& out) {
    if (width != 1) {
        return false;
    }
    if (offset == 0) {
        out = 0; // IRR/ISR reads: no interrupts latched in 6b
        return true;
    }
    if (offset == 1) {
        out = mask_;
        return true;
    }
    return false;
}

bool Pic8259::write(uint16_t offset, unsigned width, uint64_t value) {
    if (width != 1) {
        return false;
    }
    const auto byte = static_cast<uint8_t>(value);
    if (offset == 0) {
        // Command port: ICW1 (init) or OCW2/OCW3.
        if ((byte & 0x10U) != 0) { // ICW1: initialize
            initialized_ = true;
            icw_pos_ = 1;
            mask_ = 0xFF;
            vector_base_ = 0;
            (void)byte; // edge/level trigger bits do not change state in 6b
            return true;
        }
        // OCW2 (EOI) / OCW3 (read select): accepted, nothing latched.
        return true;
    }
    if (offset != 1) {
        return false;
    }
    // Data port: ICW2..ICW4 while initializing, else OCW1 (mask).
    if (initialized_ && icw_pos_ > 0) {
        switch (icw_pos_) {
        case 1:
            vector_base_ = static_cast<uint8_t>(byte & 0xF8U); // ICW2
            break;
        case 2:
            icw3_ = byte; // slave wiring
            break;
        case 3:
            icw4_ = byte; // 8086 mode
            break;
        default:
            break;
        }
        ++icw_pos_;
        if (icw_pos_ > 3) {
            icw_pos_ = 0;
        }
        return true;
    }
    mask_ = byte; // OCW1
    return true;
}

} // namespace micro_forge::kernel
