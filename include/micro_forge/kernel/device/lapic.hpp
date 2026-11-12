/**
 * @file lapic.hpp
 * @brief Local APIC (xAPIC MMIO), register surface (batch 6c).
 * @version 0.1.0
 *
 * The boot path maps the LAPIC page (MADT reports 0xFEE00000), reads
 * its ID, programs the LVT timer, and writes EOI after each interrupt.
 * The register file is a flat 4 KiB window; behavior lives in the
 * overrides (EOI clears, the current-count register answers from the
 * initial count until the tick pipeline owns decrementing in 6d).
 */

#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "micro_forge/kernel/memory/register_region.hpp"

namespace micro_forge::kernel {

/**
 * @brief The local APIC MMIO window.
 */
class Lapic : public RegisterRegionBase {
public:
    /// @brief Register offsets the boot path touches.
    enum Registers : uint16_t {
        kId = 0x020,           ///< APIC ID.
        kVersion = 0x030,      ///< Version and max LVT index.
        kTpr = 0x080,          ///< Task priority.
        kEoi = 0x0B0,          ///< End of interrupt (write-only).
        kSpurious = 0x0F0,     ///< Spurious-vector register.
        kLvtTimer = 0x320,     ///< Timer LVT entry.
        kLvtPerf = 0x340,      ///< Performance counter LVT entry.
        kLvtLint0 = 0x350,     ///< LINT0 entry.
        kLvtLint1 = 0x360,     ///< LINT1 entry.
        kLvtError = 0x370,     ///< Error LVT entry.
        kTimerInitial = 0x380, ///< Initial count.
        kTimerCurrent = 0x390, ///< Current count.
        kTimerDivide = 0x3E0,  ///< Divide configuration.
    };

    /// @brief Constructs the window (zero-initialized register file).
    Lapic();

    /// @brief Local APIC ID this machine reports (0 is the boot CPU).
    [[nodiscard]] static constexpr uint8_t id() { return 0; }
    /// @brief Whether an EOI write was observed since the last clear.
    [[nodiscard]] bool eoi_pending() const { return eoi_seen_; }
    /// @brief Clears the EOI observation flag.
    void clear_eoi() { eoi_seen_ = false; }
    /// @brief Programmed timer initial count.
    [[nodiscard]] uint32_t timer_initial() const { return timer_initial_; }

    /// @brief Binds the virtual clock (icount) the timer counts down on.
    /// @param[in] now Returns the current virtual time in instructions.
    void set_clock(std::function<uint64_t()> now) { now_ = std::move(now); }

protected:
    /// @brief Serves the read-aliased registers (timer current count).
    void on_read(uint64_t offset, unsigned width) override;
    /// @brief Captures EOI writes and timer programming.
    void on_write(uint64_t offset, unsigned width, uint64_t value) override;

private:
    std::array<uint8_t, 0x1000> window_{};
    uint32_t timer_initial_ = 0;
    uint32_t timer_divide_ = 0;
    uint64_t timer_start_ = 0;
    bool eoi_seen_ = false;
    std::function<uint64_t()> now_;
};

} // namespace micro_forge::kernel
