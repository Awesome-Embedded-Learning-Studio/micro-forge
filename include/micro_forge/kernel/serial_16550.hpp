/**
 * @file serial_16550.hpp
 * @brief COM1 16550 UART, polling surface (batch 6b).
 * @version 0.1.0
 *
 * The batch-3/tick bring-up only polls: wait for LSR.THRE, write THR.
 * Line status always reports THRE|TEMT (transmit always drainable) and
 * a receiver with no data, which is exactly how a silent UART looks to
 * the boot path. Received bytes are not modeled (no input source until
 * the shell batch).
 */

#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "micro_forge/kernel/port_space.hpp"

namespace micro_forge::kernel {

/**
 * @brief The 16550 UART register window (8 ports).
 */
class Serial16550 : public PortDevice {
public:
    /// LSR: transmit holding register empty.
    static constexpr uint8_t kLsrThre = 0x20;
    /// LSR: transmitter empty (shift register also empty).
    static constexpr uint8_t kLsrTemt = 0x40;
    /// LSR: data ready.
    static constexpr uint8_t kLsrDr = 0x01;

    /**
     * @brief Binds the transmit sink (the machine console).
     *
     * @param[in] sink Bytes written to THR are appended here.
     */
    explicit Serial16550(std::vector<char>& sink) : sink_(sink) {}

    /**
     * @brief Answers one register read (see PortDevice::read).
     *
     * @param[in] offset Register offset inside the UART window.
     * @param[in] width Access width; only 1 is served.
     * @param[out] out The register value on success.
     * @return False for unsupported widths or offsets.
     */
    bool read(uint16_t offset, unsigned width, uint64_t& out) override;

    /**
     * @brief Answers one register write (see PortDevice::write).
     *
     * @param[in] offset Register offset inside the UART window.
     * @param[in] width Access width; only 1 is served.
     * @param[in] value The value written.
     * @return False for unsupported widths or read-only offsets.
     */
    bool write(uint16_t offset, unsigned width, uint64_t value) override;

    /// @brief Queues one byte for the receiver (RBR reads consume it).
    ///
    /// @param[in] byte The byte presented as received data.
    void inject_rx(uint8_t byte) { rx_.push_back(byte); }

    /// @brief Bytes the guest has pushed through THR (diagnostic).
    [[nodiscard]] uint64_t tx_count() const { return tx_count_; }

    /// @brief Whether received data is pending (LSR.DR).
    [[nodiscard]] bool rx_pending() const { return !rx_.empty(); }

    /// @brief DLAB latch (LCR bit 7) — selects divisor vs data registers.
    [[nodiscard]] bool divider_latched() const { return (lcr_ & 0x80U) != 0; }
    /// @brief Last value written to the line control register.
    [[nodiscard]] uint8_t line_control() const { return lcr_; }

private:
    std::vector<char>& sink_;
    std::deque<uint8_t> rx_;
    uint64_t tx_count_ = 0;
    uint8_t ier_ = 0;
    uint8_t fcr_ = 0;
    uint8_t lcr_ = 0;
    uint8_t mcr_ = 0;
    uint8_t scr_ = 0;
    uint8_t dll_ = 0; // divisor low (THR path while DLAB=0 writes the sink)
    uint8_t dlm_ = 0; // divisor high
};

} // namespace micro_forge::kernel
