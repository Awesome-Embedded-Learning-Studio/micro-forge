#pragma once

#include "micro_forge/device/base/types.hpp"

#include <cstdint>
#include <expected>
#include <string_view>

namespace micro_forge::device {

/// @brief Common register, reset and clock interface for MCU devices.
struct Device {
    virtual ~Device() = default;

    virtual Expected<data_t> read(addr_t offset, Width w) = 0;
    virtual Expected<void> write(addr_t offset, data_t data, Width w) = 0;

    // Restore device state without changing observers or external connections.
    virtual void reset() {}
    virtual void tick(uint64_t /*cycles*/) {}
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // P2 fast-forward (emu_busy_wait_research): CPU cycles until this device
    // will next fire (trigger an IRQ / demand service), or 0 if it won't.
    // The coordinator uses this to skip a busy-wait straight to the next
    // event. Default 0 = device never drives a fast-forward (memories, most
    // polling peripherals). Timers override (SysTick → next COUNTFLAG reload).
    [[nodiscard]] virtual uint64_t cycles_until_next_event() const noexcept { return 0U; }
};

} // namespace micro_forge::device
