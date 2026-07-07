#pragma once

#include "cpu/cpu.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace micro_forge::chips::stm32f1 {
class Stm32f103Soc;
}

namespace micro_forge::cli {

// Structured introspection snapshot — the single source of truth for the
// observable simulator state. Consumed by both the CLI JSON serializer
// (write_snapshot_json) and the future GUI dashboard (milestone 04). The GUI
// reads these fields directly instead of re-parsing JSON text every frame.
//
// Adding a field here + filling it in read_introspection() is the only edit
// needed to surface new state to every consumer — no per-consumer scraping.

struct CpuSnapshot {
    cpu::CPU::State state = cpu::CPU::State::Halted;
    bool handler_mode = false; // true = handler, false = thread
    uint32_t pc = 0, lr = 0, sp = 0;
    std::array<uint32_t, 13> regs{}; // r0-r12
    // Status / mask / stack registers (newly exposed via CortexM3CPU getters).
    uint32_t xpsr = 0, primask = 0, basepri = 0, faultmask = 0;
    uint32_t control = 0, msp = 0, psp = 0;
};

struct FaultSnapshot {
    bool present = false;
    cpu::CPU::CPUError kind{};
    uint32_t pc = 0, lr = 0, sp = 0, xpsr = 0;
    bool is_32bit = false;
    uint16_t opcode16 = 0, opcode16_2 = 0;
    // Optional fault context — has_* is false when the source FaultRecord had
    // no value for that field. bus_error_raw is the BusError enum as a raw
    // integer so this header need not depend on memory/bus.hpp.
    bool has_bus_error = false;
    uint32_t bus_error_raw = 0;
    bool has_access_addr = false;
    uint32_t access_addr = 0;
};

struct GpioPortSnapshot {
    char port = 0;    // 'A'..'C'
    uint16_t odr = 0; // output levels; bit i = pin i high
};

struct SysTickSnapshot {
    uint32_t ctrl = 0, load = 0, val = 0; // COUNTFLAG is ctrl bit 16
};

struct NvicSnapshot {
    bool has_pending = false;
    uint8_t highest_pending_irq = 0xFF; // 0xFF = none
    uint16_t enabled_count = 0;
};

struct ScbSnapshot {
    uint32_t icsr = 0, vtor = 0, aircr = 0;
    uint8_t prigroup = 0; // AIRCR bits [10:8]
};

struct PeripheralsSnapshot {
    std::string_view usart_output;
    std::array<GpioPortSnapshot, 3> gpio{}; // ports A, B, C (in that order)
    SysTickSnapshot systick;
    NvicSnapshot nvic;
    ScbSnapshot scb;
};

struct IntrospectionSnapshot {
    CpuSnapshot cpu;
    FaultSnapshot fault;
    uint64_t cycles = 0;
    PeripheralsSnapshot peripherals;
};

// Read a structured snapshot of the SoC state. Pure read — does not mutate the
// simulator, safe to call from a GUI tick. Returns a default-constructed
// snapshot (state=Halted, all-zero) if the CPU is somehow not wired.
IntrospectionSnapshot read_introspection(chips::stm32f1::Stm32f103Soc& soc,
                                         std::string_view usart_output) noexcept;

} // namespace micro_forge::cli
