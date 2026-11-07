/**
 * @file cpu.hpp
 * @brief Long-mode x86-64 interpreter core (batch-3 execution subset).
 * @version 0.1.0
 */

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "micro_forge/arch/x86_64/decoded.hpp"
#include "micro_forge/kernel/bit.hpp"
#include "micro_forge/kernel/bus.hpp"
#include "micro_forge/kernel/scheduler.hpp"

namespace micro_forge::arch::x86_64 {

/**
 * @brief Why a CPU run stopped.
 *
 * Terminal kinds (everything past kRunning) latch: further step()/run()
 * calls replay the same status until reset().
 */
struct ExitStatus {
    /// Classification of the stop.
    enum class Kind : uint8_t {
        kRunning = 0, ///< Budget exhausted or still executing.
        kHalt,        ///< hlt retired.
        kExit,        ///< Guest syscall exit; code = rdi.
        kDebugExit,   ///< MMIO debug-exit write; code = (val << 1) | 1.
        kDecodeError, ///< Decode refused the bytes at rip; detail = kind.
        kUnsupported, ///< Decoded, outside the batch-3 subset; detail = mnemonic.
        kFault,       ///< Guest fault; detail = FaultId, code = address.
    };

    /// Stop classification.
    Kind kind = Kind::kRunning;
    /// Exit code (kExit, kDebugExit) or faulting address (kFault).
    uint64_t code = 0;
    /// RIP of the instruction that stopped the run.
    uint64_t rip = 0;
    /// Mnemonic ordinal or syscall number (kUnsupported), decode kind
    /// (kDecodeError), or FaultId.
    uint64_t detail = 0;
};

/// @brief Guest fault classification carried in ExitStatus::detail.
enum class FaultId : uint8_t {
    kDivide = 1, ///< Divide error (#DE): zero divisor or quotient overflow.
    kMemory = 2, ///< Memory access failed on the bus; code = address.
    kFetch = 3,  ///< Instruction fetch failed; code = address.
};

/**
 * @brief Flat-memory, long-mode interpreter over the batch-2 decoder.
 *
 * Execution model for batch 3:
 * - Segments are flat: es/cs/ss/ds overrides address nothing. fs/gs bases
 *   are not modeled yet, so a memory operand under fs/gs reports
 *   kUnsupported instead of guessing a base.
 * - Virtual time is icount: every retired instruction (every rep string
 *   iteration counts as one) advances the clock and dispatches the
 *   scheduler, mirroring the batch-1 FakeCpu contract.
 * - The execution subset follows the batch-2 profile: integer ALU, data
 *   movement, control flow, bit ops, string ops, and a minimal syscall
 *   surface (write collects into console(), exit/exit_group stop the
 *   run). SSE, system instructions, port IO, and CR/DR moves are decoded
 *   but report kUnsupported with the offending RIP, never silently
 *   guessed. They land with the batches that own their machinery
 *   (spec sections 5.2, 5.3).
 * - lock is accepted and ignored: with one thread, locked read-modify-
 *   write on RAM equals its unlocked form. Locked MMIO semantics arrive
 *   with the device batches.
 */
class Cpu {
public:
    /**
     * @brief Binds the CPU to its bus and scheduler.
     *
     * @param[in] bus Guest memory bus (fetch and data both).
     * @param[in] scheduler icount event queue dispatched on retire.
     */
    Cpu(kernel::Bus& bus, kernel::Scheduler& scheduler) : bus_(bus), scheduler_(scheduler) {}

    /**
     * @brief Resets architectural state and loads an entry point.
     *
     * Clears registers, flags (zf set, direction clear), icount, the
     * console, and any latched exit; then sets rip = @p rip and
     * rsp = @p rsp.
     *
     * @param[in] rip Entry rip.
     * @param[in] rsp Initial stack pointer.
     */
    void reset(uint64_t rip, uint64_t rsp);

    /**
     * @brief Debug-exit sink; wire a DebugExitRegion write into the CPU.
     *
     * Latches a kDebugExit status with @p encoded_code; the run loop
     * observes it after the current instruction completes.
     *
     * @param[in] encoded_code QEMU-style encoded exit code (val << 1) | 1.
     */
    void request_debug_exit(uint32_t encoded_code);

    /**
     * @brief Installs a per-instruction trace hook (determinism harness).
     *
     * @param[in] trace Called once per retired instruction with its RIP.
     */
    void set_trace(std::function<void(uint64_t rip)> trace) { trace_ = std::move(trace); }

    // --- state access (tests, and snapshots from batch 4) ---

    /// @brief Reads one GPR (full 64 bits).
    [[nodiscard]] uint64_t gpr(Reg reg) const { return regs_[index_of(reg)]; }
    /// @brief Writes one full GPR.
    void set_gpr(Reg reg, uint64_t value) { regs_[index_of(reg)] = value; }
    /// @brief Current rip.
    [[nodiscard]] uint64_t rip() const { return rip_; }
    /// @brief Sets rip (jump before run).
    void set_rip(uint64_t value) { rip_ = value; }
    /// @brief Retired-instruction count (virtual time).
    [[nodiscard]] kernel::Tick icount() const { return icount_; }
    /// @brief Bytes collected by the write syscall, in order.
    [[nodiscard]] const std::vector<char>& console() const { return console_; }
    /// @brief Latched exit status (kRunning while a run may continue).
    [[nodiscard]] const ExitStatus& exit_status() const { return exit_; }
    /// @brief Carry flag.
    [[nodiscard]] bool cf() const { return cf_; }
    /// @brief Zero flag.
    [[nodiscard]] bool zf() const { return zf_; }
    /// @brief Sign flag.
    [[nodiscard]] bool sf() const { return sf_; }
    /// @brief Overflow flag.
    [[nodiscard]] bool of() const { return of_; }
    /// @brief Parity flag.
    [[nodiscard]] bool pf() const { return pf_; }
    /// @brief Auxiliary-carry flag.
    [[nodiscard]] bool af() const { return af_; }
    /// @brief Direction flag (clear = string ops walk forward).
    [[nodiscard]] bool df() const { return df_; }

    // --- execution ---

    /**
     * @brief Fetches, decodes, and executes the instruction at rip.
     *
     * @return The latched status: kRunning when the budget continues,
     *         otherwise the terminal reason.
     */
    ExitStatus step();

    /**
     * @brief Runs up to @p max_instructions retired instructions.
     *
     * Stops between instructions on any terminal status. A rep-prefixed
     * string op retires inside one step (no interrupt source can split it
     * yet), so the budget is honored at instruction granularity.
     *
     * @param[in] max_instructions Instruction budget for this call.
     * @return The latched status, same contract as step().
     */
    ExitStatus run(uint64_t max_instructions);

private:
    // --- helpers, defined across cpu.cpp and execute.cpp ---

    /// Retires @p n icount units: clock advance, scheduler dispatch, trace.
    void retire(unsigned n);
    /// Fills fetch_buf_ from rip_; false on bus failure (kFetch fault).
    bool fetch(unsigned& got);
    /// Latches an unsupported-mnemonic stop for the instruction at rip_.
    ExitStatus unsupported(const DecodedInsn& insn);
    /// Effective address of a memory operand; @p rip_after resolves
    /// rip-relative forms.
    [[nodiscard]] uint64_t effective_address(const Operand& op, uint64_t rip_after) const;
    /// Reads an operand at its width; false latches a fault.
    bool read_operand(const DecodedInsn& insn, const Operand& op, uint64_t rip_after,
                      uint64_t& out);
    /// Writes an operand at its width; false latches a fault.
    bool write_operand(const DecodedInsn& insn, const Operand& op, uint64_t value);
    /// Bus load of @p width bytes; false latches kFault/kMemory.
    bool load(uint64_t address, unsigned width, uint64_t& out);
    /// Bus store of @p width bytes; false latches kFault/kMemory.
    bool store(uint64_t address, unsigned width, uint64_t value);
    /// GPR read at sub-register width (handles ah/ch/dh/bh aliasing).
    [[nodiscard]] uint64_t read_gpr(Reg reg, unsigned width, bool rex) const;
    /// GPR write at sub-register width (32-bit writes zero-extend).
    void write_gpr(Reg reg, unsigned width, bool rex, uint64_t value);
    /// Evaluates a condition code against the flags.
    [[nodiscard]] bool condition(Cc cc) const;
    /// Sign bit mask helper for @p width bytes.
    [[nodiscard]] static uint64_t sign_bit(unsigned width) {
        return kernel::bit::mask_bit((8U * width) - 1U);
    }
    /// Full width mask for @p width bytes.
    [[nodiscard]] static uint64_t width_mask(unsigned width) {
        return kernel::bit::ones(8U * width);
    }
    /// Sign-extends @p value from @p width bytes to 64 bits.
    [[nodiscard]] static uint64_t sign_extend(uint64_t value, unsigned width) {
        const unsigned bits = 8U * width;
        if (bits >= 64U) {
            return value;
        }
        const uint64_t sign = kernel::bit::mask_bit(bits - 1U);
        const uint64_t masked = value & kernel::bit::ones(bits);
        return (masked ^ sign) - sign; // branchless two's-complement extend
    }
    /// Parity of the low byte (x86 PF: 1 when an even number of set bits).
    [[nodiscard]] static bool parity_of(uint64_t value);
    /// Sets zf/sf/pf from @p result at @p width.
    void set_zsp(uint64_t result, unsigned width);
    /// Flag-recording add; @p carry_in for adc. Returns the masked result.
    uint64_t alu_add(uint64_t a, uint64_t b, unsigned width, bool carry_in);
    /// Flag-recording subtract (sub/sbb/cmp/neg bodies).
    uint64_t alu_sub(uint64_t a, uint64_t b, unsigned width, bool borrow_in);
    /// Flag-recording logic (and/or/xor/test set cf=of=0, zsp from result).
    uint64_t alu_logic(uint64_t result, unsigned width);
    /// Flag-recording single rotate/shift dispatch for the grp2 family.
    bool alu_shift(Mnemonic mn, uint64_t& value, unsigned width, uint64_t count);
    /// One-operand mul/imul/div/idiv (grp3) on rax:rdx pairs.
    ExitStatus execute_grp3(const DecodedInsn& insn, uint64_t src);
    /// Two-operand (0F AF) and three-operand (69/6B) imul forms.
    ExitStatus execute_imul23(const DecodedInsn& insn, uint64_t rip_after);
    /// String-op family (movs/stos/lods/scas/cmps) incl. rep iterations.
    ExitStatus execute_string(const DecodedInsn& insn, uint64_t rip_after);
    /// Minimal syscall surface; everything else is kUnsupported.
    ExitStatus execute_syscall();
    /// The big dispatch for everything except string ops and syscall.
    ExitStatus execute(const DecodedInsn& insn, uint64_t rip_after);
    /// Pushes @p value at @p width (default 8); false latches a fault.
    bool push_value(uint64_t value, unsigned width);
    /// Pops @p width bytes into @p out; false latches a fault.
    bool pop_value(unsigned width, uint64_t& out);
    /// Latches @p status if terminal and reports whether to stop.
    bool latch(ExitStatus status);

    // --- state ---

    kernel::Bus& bus_;
    kernel::Scheduler& scheduler_;
    std::array<uint64_t, 16> regs_{};
    uint64_t rip_ = 0;
    // Scalar flags; composed into rflags when snapshots need it (batch 4).
    bool cf_ = false;
    bool pf_ = false;
    bool af_ = false;
    bool zf_ = false;
    bool sf_ = false;
    bool of_ = false;
    bool df_ = false;
    kernel::Tick icount_ = 0;
    std::vector<char> console_;
    std::function<void(uint64_t rip)> trace_;
    /// RIP of the instruction being executed (diagnostics and traces).
    uint64_t insn_rip_ = 0;
    /// Length in bytes of the instruction being executed.
    uint8_t cur_length_ = 0;
    ExitStatus exit_{};
    std::array<uint8_t, 16> fetch_buf_{};

    /// GPR array index of a register enum.
    [[nodiscard]] static unsigned index_of(Reg reg) { return static_cast<unsigned>(reg) & 0xFU; }
};

} // namespace micro_forge::arch::x86_64
