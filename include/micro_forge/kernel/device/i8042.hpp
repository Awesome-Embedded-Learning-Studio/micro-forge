/**
 * @file i8042.hpp
 * @brief The 8042 keyboard controller: polling handshake (batch 6d).
 * @version 0.1.0
 *
 * The boot path runs the standard controller handshake: disable both
 * ports, read the configuration byte, rewrite it, run the self-test and
 * expect 0x55, then enable the first port. The model keeps an output
 * queue (OBF) and the configuration byte; scancode injection and the
 * command set beyond this are the batch-6d keyboard work.
 */

#pragma once

#include <cstdint>
#include <deque>

#include "micro_forge/kernel/bus/port_space.hpp"

namespace micro_forge::kernel {

/// State shared by the 0x60 (data) and 0x64 (status/command) views.
struct I8042State {
    /// Bytes waiting in the output buffer (OBF when non-empty).
    std::deque<uint8_t> output;
    /// Controller configuration byte.
    uint8_t config = 0x00;
};

/**
 * @brief One side of the 8042 register pair.
 */
class I8042 : public PortDevice {
public:
    /// @brief Which register this instance serves.
    enum Port : uint8_t {
        kData = 0x60,   ///< Data port.
        kStatus = 0x64, ///< Status/command port.
    };

    /// @brief Constructs one side over shared state.
    /// @param[in] port Which register this instance serves.
    /// @param[in] state State shared with the other side; caller owns it.
    I8042(Port port, I8042State& state) : port_(port), state_(state) {}

    /**
     * @brief Answers one controller read.
     * @param[in] offset Always 0 (single-port window).
     * @param[in] width Access width; only 1 is served.
     * @param[out] out Status byte or the next output byte.
     * @return False for unsupported widths.
     */
    bool read(uint16_t offset, unsigned width, uint64_t& out) override {
        (void)offset;
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

    /**
     * @brief Answers one controller write.
     * @param[in] offset Always 0 (single-port window).
     * @param[in] width Access width; only 1 is served.
     * @param[in] value The byte written.
     * @return False for unsupported widths.
     */
    bool write(uint16_t offset, unsigned width, uint64_t value) override {
        (void)offset;
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

private:
    Port port_;
    I8042State& state_;
};

} // namespace micro_forge::kernel
