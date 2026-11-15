/**
 * @file serial_16550.cpp
 * @brief COM1 polling semantics (batch 6b).
 * @version 0.1.0
 */

#include "micro_forge/device/pc/serial_16550.hpp"
#include <cstdint>

namespace micro_forge::device::pc {

bool Serial16550::read(uint16_t offset, unsigned width, uint64_t& out) {
    if (width != 1) {
        return false; // the UART answers byte accesses only
    }
    uint8_t value = 0;
    switch (offset) {
    case 0: { // RBR (consumes one received byte) or divisor low
        if (divider_latched()) {
            value = dll_;
            break;
        }
        if (core_.rx_ready()) {
            value = core_.read_rx();
        }
        break;
    }
    case 1: // IER or divisor high
        value = divider_latched() ? dlm_ : ier_;
        break;
    case 2: // IIR: no interrupt pending, 16550 FIFO flags
        value = static_cast<uint8_t>(0x01U | (fcr_ & 0xC0U));
        break;
    case 3: // LCR
        value = lcr_;
        break;
    case 4: // MCR
        value = mcr_;
        break;
    case 5: { // LSR: transmitter always drainable; DR when rx pending
        const auto dr = core_.rx_ready() ? static_cast<unsigned>(kLsrDr) : 0U;
        value = static_cast<uint8_t>(static_cast<unsigned>(kLsrThre | kLsrTemt) | dr);
        break;
    }
    case 6: // MSR: CTS | DSR | DCD (a plugged-in line)
        value = 0xB0U;
        break;
    case 7: // SCR
        value = scr_;
        break;
    default:
        return false;
    }
    out = value;
    return true;
}

bool Serial16550::write(uint16_t offset, unsigned width, uint64_t value) {
    if (width != 1) {
        return false;
    }
    const auto byte = static_cast<uint8_t>(value);
    switch (offset) {
    case 0:
        if (divider_latched()) {
            dll_ = byte;
        } else {
            core_.transmit(byte); // THR
        }
        break;
    case 1:
        if (divider_latched()) {
            dlm_ = byte;
        } else {
            ier_ = byte;
        }
        break;
    case 2:
        fcr_ = byte;
        break;
    case 3:
        lcr_ = byte;
        break;
    case 4:
        mcr_ = byte;
        break;
    case 7:
        scr_ = byte;
        break;
    default:
        return false; // LSR/MSR are read-only
    }
    return true;
}

} // namespace micro_forge::device::pc
