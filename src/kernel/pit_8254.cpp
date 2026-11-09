/**
 * @file pit_8254.cpp
 * @brief 8254 and 0x61 board port semantics (batch 6b).
 * @version 0.1.0
 */

#include "micro_forge/kernel/pit_8254.hpp"

namespace micro_forge::kernel {

bool Pit8254::read(uint16_t offset, unsigned width, uint64_t& out) {
    if (width != 1) {
        return false;
    }
    if (offset >= 3) {
        return false; // port 0x43 is write-only
    }
    const Channel& ch = channels_[offset];
    // Reading a counter returns the current count; the deterministic
    // value is the programmed divisor's byte for the selected access.
    uint8_t value = 0;
    if (ch.access == 3) { // lo-hi: alternates like the real latch
        value = (ch.writes % 2U == 0U) ? static_cast<uint8_t>(ch.divisor & 0xFFU)
                                       : static_cast<uint8_t>(ch.divisor >> 8U);
    } else if (ch.access == 2) {
        value = static_cast<uint8_t>(ch.divisor >> 8U);
    } else {
        value = static_cast<uint8_t>(ch.divisor & 0xFFU);
    }
    out = value;
    return true;
}

bool Pit8254::write(uint16_t offset, unsigned width, uint64_t value) {
    if (width != 1) {
        return false;
    }
    const auto byte = static_cast<uint8_t>(value);
    if (offset == 3) {
        // Control word: channel select (6..7), access mode (4..5), mode.
        control_ = byte;
        const unsigned channel = (static_cast<unsigned>(byte) >> 6U) & 0x3U;
        if (channel != 3) { // 3 = read-back command, not modeled
            Channel& ch = channels_[channel];
            ch.access = static_cast<uint8_t>((static_cast<unsigned>(byte) >> 4U) & 0x3U);
            ch.mode = static_cast<uint8_t>((static_cast<unsigned>(byte) >> 1U) & 0x7U);
            ch.writes = 0;
        }
        return true;
    }
    if (offset >= 3) {
        return false;
    }
    Channel& ch = channels_[offset];
    if (ch.access == 1) { // low byte only
        ch.divisor = byte;
    } else if (ch.access == 2) { // high byte only
        ch.divisor = static_cast<uint16_t>(static_cast<uint16_t>(byte) << 8U);
    } else { // lo-hi (or latch read sequence): assemble two bytes
        if (ch.writes % 2U == 0U) {
            ch.lo_byte = byte;
            ch.divisor = static_cast<uint16_t>(ch.divisor & 0xFF00U);
        } else {
            ch.divisor = static_cast<uint16_t>(static_cast<uint16_t>(byte) << 8U) | ch.lo_byte;
        }
    }
    ++ch.writes;
    return true;
}

bool BoardControl::read(uint16_t offset, unsigned width, uint64_t& out) {
    (void)offset;
    if (width != 1) {
        return false;
    }
    // Bit 5 (timer 2 output) always reads done: the delay loop over the
    // one-shot channel must terminate under the virtual clock.
    out = static_cast<uint64_t>(latches_ | 0x20U);
    return true;
}

bool BoardControl::write(uint16_t offset, unsigned width, uint64_t value) {
    (void)offset;
    if (width != 1) {
        return false;
    }
    latches_ = static_cast<uint8_t>(value & 0x03U);
    return true;
}

} // namespace micro_forge::kernel
