#pragma once
#include "autogen/arch_details.hpp"
#include "intr.hpp"
#include <cstdint>
#include <expected>
#include <string_view>
namespace micro_forge::cpu {

struct CPU {
    enum class CPUError {
        NotRunning,
        RegisterIndexOverflow,
        FailedPollIntr,
        InstructionFetchFault,
        DataAccessFault,
        IllegalInstruction,
        ExceptionEntryFault,
        ExceptionReturnFault,
        InvalidPc,
    };
    template <typename T> using CPUExpected = std::expected<T, CPUError>;

    /* Man we think uint64_t is enough! if it overflows again, love the
     * BigInterger! I Think! */
    using ticks_t = uint64_t;
    enum class State {
        Running, // Normal execution
        Halted,  // Stopped by HALT instruction
        Faulted, // Stopped by undefined instruction or bus error
    };
    virtual ~CPU() = default;

    virtual CPUExpected<void> reset() = 0;
    virtual CPUExpected<void> step() = 0;
    virtual CPUExpected<State> state() const noexcept = 0;

    virtual CPUExpected<data_t>
    register_value(const std::size_t index) const = 0;
    virtual CPUExpected<void> set_register_value(const std::size_t index,
                                                 data_t value) = 0;
    virtual CPUExpected<std::string_view>
    register_name(std::size_t idx) const = 0;
    virtual CPUExpected<std::size_t> register_count() const = 0;
    /* Architeture CPU instractions index */
    virtual CPUExpected<addr_t> pc() const = 0;
    virtual CPUExpected<addr_t> set_pc(addr_t new_pc) = 0;

    /* Intr */
    virtual CPUExpected<void> raise_irq(intr::intr_n_t irq_index) = 0;
    /* CPU has ticks, so that is it! */
    virtual CPUExpected<ticks_t> cycles() const = 0;
    // P2 fast-forward (emu_busy_wait_research): advance the cycle counter by
    // n WITHOUT executing instructions. Lets the coordinator skip a busy-wait
    // while keeping cpu cycles ↔ virtual-clock lockstep. Default no-op;
    // Cortex-M3 overrides.
    virtual CPUExpected<void> advance_cycles(ticks_t /*n*/) { return {}; }
    // P2.a WFI fast-forward: true while suspended on WFI (definitive sleep,
    // not a hint). Default false; Cortex-M3 overrides.
    virtual bool is_sleeping() const noexcept { return false; }
};
} // namespace micro_forge::cpu
