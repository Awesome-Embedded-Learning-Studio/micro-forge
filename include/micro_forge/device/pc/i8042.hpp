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

#include "micro_forge/arch/x86_64/port_space.hpp"

namespace micro_forge::device::pc {

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
class I8042 : public arch::x86_64::PortDevice {
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
    bool read(uint16_t /*offset*/, unsigned width, uint64_t& out) override;

    /**
     * @brief Answers one controller write.
     * @param[in] offset Always 0 (single-port window).
     * @param[in] width Access width; only 1 is served.
     * @param[in] value The byte written.
     * @return False for unsupported widths.
     */
    bool write(uint16_t /*offset*/, unsigned width, uint64_t value) override;

private:
    Port port_;
    I8042State& state_;
};

} // namespace micro_forge::device::pc
