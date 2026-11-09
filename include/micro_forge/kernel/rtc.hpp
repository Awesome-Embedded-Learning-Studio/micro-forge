/**
 * @file rtc.hpp
 * @brief CMOS real-time clock, deterministic wall clock (batch 6b).
 * @version 0.1.0
 *
 * Two ports: 0x70 selects a CMOS register, 0x71 reads or writes it.
 * The v0 clock is a fixed BCD time derived from the machine seed (batch
 * 8 owns seed derivation); until then it is a constant, which keeps the
 * boot path deterministic. Status A reports no update in progress.
 */

#pragma once

#include <array>
#include <cstdint>

#include "micro_forge/kernel/port_space.hpp"

namespace micro_forge::kernel {

/**
 * @brief The 0x70/0x71 CMOS clock window.
 */
class Rtc : public PortDevice {
public:
    /**
     * @brief Answers one CMOS access (see PortDevice::read).
     * @param[in] offset 0 = index latch, 1 = data port.
     * @param[in] width Access width; only 1 is served.
     * @param[out] out The register value on success.
     * @return False for unsupported widths or offsets.
     */
    bool read(uint16_t offset, unsigned width, uint64_t& out) override;

    /**
     * @brief Answers one CMOS access (see PortDevice::write).
     * @param[in] offset 0 = index latch, 1 = data port.
     * @param[in] width Access width; only 1 is served.
     * @param[in] value The value written.
     * @return False for unsupported widths or offsets.
     */
    bool write(uint16_t offset, unsigned width, uint64_t value) override;

    /// @brief The BCD time this clock reports.
    [[nodiscard]] const std::array<uint8_t, 10>& registers() const { return registers_; }

private:
    /// CMOS register image, BCD: 00 sec, 02 min, 04 hour, 07 day,
    /// 08 month, 09 year, 0A status A, 0B status B, 0C/0D status C/D.
    std::array<uint8_t, 10> registers_ = {
        0x00, // 00 seconds        (00)
        0x00, // 01 seconds alarm  (unused)
        0x00, // 02 minutes        (00)
        0x00, // 03 minutes alarm  (unused)
        0x12, // 04 hours          (12, 24h)
        0x00, // 05 hours alarm    (unused)
        0x00, // 06 weekday        (unused)
        0x01, // 07 day of month   (01)
        0x01, // 08 month          (01)
        0x26, // 09 year           (26 => 2026)
    };
    uint8_t index_ = 0;
    // Index-alias map: status registers live at 0x0A..0x0D outside the
    // 10-entry time image above.
    uint8_t status_a_ = 0x26; // no UIP, 32 kHz period
    uint8_t status_b_ = 0x02; // 24-hour BCD
    uint8_t status_c_ = 0x00;
    uint8_t status_d_ = 0x80; // valid RAM/time
};

} // namespace micro_forge::kernel
