/**
 * @file pit_8254.hpp
 * @brief 8254 PIT and the 0x61 board control port (batch 6b).
 * @version 0.1.0
 *
 * The boot path uses two channels: ch0 in square-wave mode for the
 * tick, and ch2 in one-shot mode for microsecond delays polled through
 * the 0x61 board control port. The virtual clock advances with icount,
 * so the delay port reports "output done" immediately (bit 5 always
 * set): the wait loop must never spin on wall time, and the device's
 * real periodicity belongs to the interrupt pipeline (batch 6c).
 */

#pragma once

#include <cstdint>

#include "micro_forge/kernel/bus/port_space.hpp"

namespace micro_forge::kernel {

/**
 * @brief The three PIT channels and their control word latch.
 */
class Pit8254 : public PortDevice {
public:
    /// @brief PIT input clock in Hz (the 1.193182 MHz crystal).
    static constexpr uint32_t kInputHz = 1193182;

    /**
     * @brief Answers one PIT register access (see PortDevice::read).
     * @param[in] offset Register offset (0..2 channels, 3 control).
     * @param[in] width Access width; only 1 is served.
     * @param[out] out The register value on success.
     * @return False for unsupported widths or offsets.
     */
    bool read(uint16_t offset, unsigned width, uint64_t& out) override;

    /**
     * @brief Answers one PIT register access (see PortDevice::write).
     * @param[in] offset Register offset (0..2 channels, 3 control).
     * @param[in] width Access width; only 1 is served.
     * @param[in] value The value written.
     * @return False for unsupported widths or offsets.
     */
    bool write(uint16_t offset, unsigned width, uint64_t value) override;

    /// @brief Programmed divisor of one channel (0 when unprogrammed).
    [[nodiscard]] uint16_t divisor(unsigned channel) const { return channels_[channel].divisor; }
    /// @brief Last control word written to port 0x43.
    [[nodiscard]] uint8_t control_word() const { return control_; }

private:
    struct Channel {
        uint8_t access = 0;   // 0 = latch, 1 = lo, 2 = hi, 3 = lo-hi
        uint8_t mode = 0;     // counter mode (2, 3, ...)
        uint8_t lo_byte = 0;  // first data byte of the current program
        uint16_t divisor = 0; // assembled divisor
        unsigned writes = 0;  // data bytes written since the control word
    };
    Channel channels_[3];
    uint8_t control_ = 0;
};

/**
 * @brief The 0x61 board control port: timer gate, speaker, output state.
 */
class BoardControl : public PortDevice {
public:
    /**
     * @brief Answers one board-port read (see PortDevice::read).
     * @param[in] offset Always 0 (single-port window).
     * @param[in] width Access width; only 1 is served.
     * @param[out] out The port value: bit 0/1 latches, bit 5 out2 done.
     * @return False for unsupported widths.
     */
    bool read(uint16_t offset, unsigned width, uint64_t& out) override;

    /**
     * @brief Answers one board-port write (see PortDevice::write).
     * @param[in] offset Always 0 (single-port window).
     * @param[in] width Access width; only 1 is served.
     * @param[in] value Bits 0/1 are latched.
     * @return False for unsupported widths.
     */
    bool write(uint16_t offset, unsigned width, uint64_t value) override;

private:
    uint8_t latches_ = 0; // bit0 timer-2 gate, bit1 speaker data
};

} // namespace micro_forge::kernel
