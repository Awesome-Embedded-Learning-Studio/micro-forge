/**
 * @file cpu.hpp
 * @brief Long-mode x86-64 interpreter core (batch-3 execution subset).
 * @version 0.1.0
 */

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "micro_forge/arch/x86_64/cached_insn.hpp"
#include "micro_forge/arch/x86_64/mmu.hpp"
#include "micro_forge/base/cache.hpp"
#include "micro_forge/base/jit.hpp"
#include "micro_forge/base/jit_translate.hpp"
#include "micro_forge/kernel/bit.hpp"
#include "micro_forge/kernel/bus.hpp"
#include "micro_forge/kernel/irq_line.hpp"
#include "micro_forge/kernel/port_space.hpp"
#include "micro_forge/kernel/scheduler.hpp"
#include "micro_forge/kernel/snapshot.hpp"

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
    kMsr = 4,    ///< MSR outside the whitelist (#GP); code = the index.
    kUd = 5,     ///< Undefined instruction (#UD, ud2); code = rip.
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
// The state object is large by design (snapshots, boot handoff); a few
// padding bytes between subsystems are irrelevant next to the caches it
// already misses on. Suppress the opt-in padding analyzer.
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
class Cpu {
public:
    /**
     * @brief Binds the CPU to its bus and scheduler.
     *
     * @param[in] bus Guest memory bus (fetch and data both).
     * @param[in] scheduler icount event queue dispatched on retire.
     */
    Cpu(kernel::Bus& bus, kernel::Scheduler& scheduler, kernel::PortSpace* ports = nullptr)
        : bus_(bus), scheduler_(scheduler), ports_(ports) {}

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
     * @brief Sizes the caches for a machine with @p ram_size bytes.
     *
     * The code-page bitmap covers exactly the physical pages of the
     * machine (no hashing, no collisions); the decode cache's active
     * capacity becomes @p decode_capacity entries (0 disables it, the
     * compile-time bound caps it). Called by the machine after
     * construction; CPU-only use leaves the defaults.
     *
     * @param[in] ram_size Guest RAM size in bytes.
     * @param[in] decode_capacity Decode-cache entries (0 = disabled).
     */
    void configure_caches(uint64_t ram_size, std::size_t decode_capacity = 512);

    /// @brief Decode-cache capacity configured for this CPU.
    [[nodiscard]] std::size_t decode_cache_capacity() const { return decode_cache_.capacity(); }
    /// @brief Decode-cache hit/miss counters.
    [[nodiscard]] auto decode_cache_stats() const { return decode_cache_.stats(); }

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

    /**
     * @brief Attaches an interrupt line with its receiving hook.
     *
     * After every retired instruction (and after scheduler dispatch) the
     * CPU observes the line; when it is high, @p on_pending fires with
     * the icount as-of. The hook is the batch-4 "receiving point": real
     * vector injection replaces it in batch 6. The typical hook
     * acknowledges the line (lower()), so a level line does not re-fire
     * on the next instruction.
     *
     * @param[in] line The interrupt line to observe; caller owns it.
     * @param[in] on_pending Called once per observation while the line
     *            is high; may be null (observe via irq_pending() only).
     */
    void attach_irq(kernel::IrqLine& line, std::function<void()> on_pending = nullptr) {
        irq_line_ = &line;
        irq_hook_ = std::move(on_pending);
    }

    /// @brief Whether the attached line (if any) is high.
    [[nodiscard]] bool irq_pending() const { return irq_line_ != nullptr && irq_line_->pending(); }

    /// @brief Queues one interrupt vector for delivery (batch 6c).
    ///
    /// The vector is delivered at the next retire boundary when IF is
    /// set and the machine is running: the IDT gate is looked up, the
    /// five-qword interrupt frame is pushed (SS, RSP, RFLAGS, CS, RIP
    /// in 64-bit mode), IF is cleared for interrupt gates, and RIP jumps
    /// to the handler. A missing IDT (base 0) drops the request.
    ///
    /// @param[in] vector IDT vector to deliver (0..255).
    void request_interrupt(uint8_t vector) {
        pending_vector_ = vector;
        interrupt_pending_ = true;
    }
    /// @brief Whether an interrupt request is queued.
    [[nodiscard]] bool interrupt_queued() const { return interrupt_pending_; }

    // --- state access (tests, and snapshots from batch 4) ---

    /// @brief Reads one GPR (full 64 bits).
    [[nodiscard]] uint64_t gpr(Reg reg) const { return regs_[index_of(reg)]; }
    /// Probe-only: byte image of the faulting instruction and its length.
    [[nodiscard]] std::span<const uint8_t> last_fault_bytes() const {
        return {last_fault_bytes_.data(), last_fault_bytes_.size()};
    }
    /// Probe-only: execution length of the faulting instruction.
    [[nodiscard]] unsigned last_fault_length() const { return last_fault_length_; }
    /// @brief Writes one full GPR.
    void set_gpr(Reg reg, uint64_t value) { regs_[index_of(reg)] = value; }
    /// @brief Current rip.
    [[nodiscard]] uint64_t rip() const { return rip_; }
    /// @brief Sets rip (jump before run).
    void set_rip(uint64_t value) { rip_ = value; }
    /// @brief Translates an instruction fetch.
    /// @param[in] vaddr Guest virtual address.
    /// @return The physical address (0 when it faults).
    uint64_t fetch_paddr(uint64_t vaddr);

    /// @brief JIT load helper.
    /// @param[in] vaddr Guest virtual address.
    /// @param[in] width Access width in bytes.
    /// @return Loaded value, or 0 when translation fails.
    uint64_t jit_load(uint64_t vaddr, unsigned width);
    /// @brief JIT helper: GPR[slot] += imm, sum with ADD flags.
    /// @param[in] slot Register slot offset (reg * 8).
    /// @param[in] imm Immediate value.
    /// @return Updated register value.
    uint64_t jit_add(uint64_t slot, uint64_t imm);
    /// @brief JIT helper: store @p width bytes at @p vaddr.
    void jit_store(uint64_t vaddr, unsigned width, uint64_t value);
    /// @brief JIT helper: qword [vaddr] += value, updating ADD flags.
    void jit_add_mem(uint64_t vaddr, uint64_t value);
    /// @brief Restricts JIT compilation to [lo, hi); zero means no filter.
    void set_code_range(uint64_t lo, uint64_t hi) {
        code_lo_ = lo;
        code_hi_ = hi;
    }
    /// @brief Turns block compilation on without the env variable.
    void set_jit_enabled(bool enabled) { jit_requested_ = enabled; }
    /// @brief JIT counters: coverage meter plus block/failed-rip counts.
    [[nodiscard]] std::uint64_t jit_insn_count() const { return jit_insn_count_; }
    [[nodiscard]] std::size_t jit_block_count() const { return jit_blocks_.size(); }
    [[nodiscard]] std::size_t jit_failed_count() const { return jit_failed_.size(); }

    /// @brief Retired-instruction count (virtual time).
    [[nodiscard]] kernel::Tick icount() const { return icount_; }
    /// @brief Bytes collected by the write syscall, in order.
    [[nodiscard]] const std::vector<char>& console() const { return console_; }
    /// @brief Mutable console sink (devices append their output here).
    [[nodiscard]] std::vector<char>& console_sink() { return console_; }
    /// @brief Latched exit status (kRunning while a run may continue).
    [[nodiscard]] const ExitStatus& exit_status() const { return exit_; }

    /// @brief rflags image (reserved bit 1 set, IF from if_).
    [[nodiscard]] uint64_t rflags() const;
    /// @brief Decomposes an rflags image into the stored flags.
    void set_rflags(uint64_t value);
    /// @brief Interrupt-enable state (IF).
    [[nodiscard]] bool interrupts_enabled() const { return if_; }
    /// @brief Current privilege level (0 = kernel, 3 = user).
    [[nodiscard]] unsigned cpl() const { return cpl_; }    /// @brief Reads one control register (0=CR0, 1=CR2, 2=CR3, 3=CR4).
    [[nodiscard]] uint64_t control_register(unsigned index) const { return cr_[index]; }
    /// @brief Writes one control register by enumeration offset.
    void set_control_register(unsigned index, uint64_t value) { cr_[index] = value; }
    /// @brief Enables paging (boot shim) or returns to pass-through.
    void set_paging(bool enabled) { mmu_.set_paging(enabled); }
    /// @brief Flushes the translation cache (CR3 write).
    void flush_tlb() { mmu_.flush(); }
    /// @brief Drops one translation (INVLPG).
    void invalidate_page(uint64_t vaddr) { mmu_.invalidate(vaddr); }
    /// @brief CR2 of the last page fault.
    [[nodiscard]] uint64_t page_fault_address() const { return cr2_; }
    /// @brief #PF error code of the last page fault.
    [[nodiscard]] uint64_t page_fault_error() const { return pf_error_; }
    /// @brief Reads one model-specific register from the whitelist.
    [[nodiscard]] std::optional<uint64_t> read_msr(uint32_t index) const;
    /// @brief Writes one model-specific register.
    ///
    /// @param[in] index MSR index; must be whitelisted.
    /// @param[in] value Full 64-bit value (EDX:EAX packed by the caller).
    /// @return false when the index is not whitelisted (the executor
    ///         reports a #GP-style fault).
    bool write_msr(uint32_t index, uint64_t value);
    /// @brief One CPUID answer.
    struct CpuidAnswer {
        /// EAX of the answer.
        uint32_t eax = 0;
        /// EBX of the answer.
        uint32_t ebx = 0;
        /// ECX of the answer.
        uint32_t ecx = 0;
        /// EDX of the answer.
        uint32_t edx = 0;
    };
    /// @brief Answers one CPUID leaf from the fixed v0 self-description.
    [[nodiscard]] static CpuidAnswer cpuid(uint32_t leaf, uint32_t subleaf);
    /// @brief Reads one XMM register (low, high 64-bit halves).
    [[nodiscard]] std::array<uint64_t, 2> xmm(Reg reg) const {
        const auto i = static_cast<unsigned>(reg) - 16U;
        return xmm_[i];
    }
    /// @brief Writes one XMM register.
    void set_xmm(Reg reg, std::array<uint64_t, 2> value) {
        xmm_[static_cast<unsigned>(reg) - 16U] = value;
    }
    /// @brief Sets the GDTR image (the boot shim installs its own).
    void set_gdtr(uint64_t base, uint16_t limit) {
        gdtr_base_ = base;
        gdtr_limit_ = limit;
    }
    /// @brief GS base (swapgs swaps this with the kernel base).
    [[nodiscard]] uint64_t gs_base() const { return gs_base_; }
    /// @brief GS base setter (MSR 0xC0000101 writes land here).
    void set_gs_base(uint64_t value) { gs_base_ = value; }
    /// @brief Swaps GS base with IA32_KERNEL_GS_BASE.
    void swapgs_state() { std::swap(gs_base_, kernel_gs_base_); }

    /**
     * @brief Serializes architectural state (regs, rip, flags, icount,
     *        console) into @p writer.
     *
     * Snapshots are taken between instructions (run() returns at a
     * boundary); mid-instruction state is not representable in v0.
     *
     * @param[in,out] writer Snapshot payload writer.
     */
    void save(kernel::SnapshotWriter& writer) const;

    /**
     * @brief Replaces architectural state from @p reader and clears any
     *        latched exit.
     *
     * @param[in,out] reader Positioned snapshot reader.
     * @return Success; false on short data.
     */
    [[nodiscard]] bool restore(kernel::SnapshotReader& reader);
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
    [[nodiscard]] ExitStatus step();

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
    [[nodiscard]] ExitStatus run(uint64_t max_instructions);

private:
    // --- helpers, defined across cpu.cpp and execute.cpp ---

    /// Applies the FS/GS segment base for a memory operand's EA.
    [[nodiscard]] uint64_t segment_address(const DecodedInsn& insn, uint64_t ea) const;
    /// Retires @p n icount units: clock advance, scheduler dispatch, irq
    /// observation, trace.
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
    /// x86 shift-count mask: 6 bits at 64-bit operands, else 5 bits.
    [[nodiscard]] static uint64_t shift_count_mask(unsigned width) {
        return width == 8U ? 0x3FULL : 0x1FULL;
    }
    /// Sign-extends @p value from @p width bytes to 64 bits.
    [[nodiscard]] static uint64_t sign_extend(uint64_t value, unsigned width) {
        return kernel::bit::sign_extend(value, 8U * width);
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
    ExitStatus execute_syscall(uint64_t rip_after);
    /// The big dispatch for everything except string ops and syscall.
    ExitStatus execute(const DecodedInsn& insn, uint64_t rip_after);
    /// Translates one guest access through the MMU (pass-through when
    /// paging is off). Records CR2 and the error code on a fault; the
    /// #PF frame's RIP is @p fault_rip (the faulting instruction).
    [[nodiscard]] TranslateResult translate_access(uint64_t vaddr, bool write, bool instruction,
                                                   uint64_t fault_rip);
    /// Raises #PF (vector 14) with its error code and frame RIP, or
    /// returns false when there is no IDT to deliver into.
    bool raise_page_fault(uint64_t error, uint64_t fault_rip);
    /// Port I/O routing (io.cpp).
    ExitStatus execute_ports(const DecodedInsn& insn, uint64_t rip_after);
    /// The IRET return path (interrupt.cpp).
    ExitStatus execute_iretq();
    /// The far-return path used by the boot world switch (interrupt.cpp).
    ExitStatus execute_lretq();
    /// SYSRET: return to user mode (interrupt.cpp).
    ExitStatus execute_sysretq();
    /// Dispatches the three return instructions (interrupt.cpp).
    ExitStatus execute_return(const DecodedInsn& insn);
    /// System instructions and system-register moves (sysregs.cpp).
    ExitStatus execute_system(const DecodedInsn& insn, uint64_t rip_after);
    /// Dispatches one decoded instruction (string op / syscall / regular).
    ExitStatus execute_insn(const DecodedInsn& insn, uint64_t rip_after);
    /// Executes a prevalidated common form or uses the full executor.
    ExitStatus execute_cached(const CachedInsn& cached, uint64_t rip_after);
    /// SSE/SSE2 subset execution (sse.cpp).
    ExitStatus execute_sse(const DecodedInsn& insn, uint64_t rip_after);
    /// New-kernel SSE face via execute_sse default (sse_ext.cpp).
    ExitStatus execute_sse_ext(const DecodedInsn& insn, uint64_t rip_after);
    /// Whether the mnemonic belongs to the SSE executor (sse.cpp).
    [[nodiscard]] static bool is_sse_mnemonic(Mnemonic mnemonic);
    /// Pushes @p value at @p width (default 8); false latches a fault.
    bool push_value(uint64_t value, unsigned width);
    /// Pops @p width bytes into @p out; false latches a fault.
    bool pop_value(unsigned width, uint64_t& out);
    /// Latches @p status if terminal and reports whether to stop.
    bool latch(ExitStatus status);

    // --- state ---

    kernel::Bus& bus_;
    kernel::Scheduler& scheduler_;
    kernel::PortSpace* ports_;

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
    bool if_ = false, ac_ = false;
    /// Control registers by enumeration offset (0=CR0, 1=CR2, 2=CR3,
    /// 3=CR4); values are stored for batch 6a, paging arrives with the MMU.
    std::array<uint64_t, 4> cr_{};
    /// Debug registers DR0-3/6/7 (indexed by register number).
    std::array<uint64_t, 8> dr_{};
    /// GDTR/IDTR base and limit (system instructions store them here).
    uint64_t gdtr_base_ = 0, idtr_base_ = 0;
    uint16_t gdtr_limit_ = 0, idtr_limit_ = 0;
    /// MSR space: only whitelisted indices live here (Cinux boot set).
    std::unordered_map<uint32_t, uint64_t> msr_;
    /// Current privilege level (0 kernel, 3 user); set by sysretq/iretq
    /// and syscall (batch 6d).
    unsigned cpl_ = 0;
    /// Set when a #PF was injected instead of latching a fault, so the
    /// access helpers know not to latch.
    bool exception_injected_ = false;
    /// Recursion depth of #PF injection (a fault inside the injection
    /// frame is not delivered again; it latches as a terminal fault).
    unsigned exception_depth_ = 0;
    /// Cached IA32_EFER: the hot translate path reads it per access, and
    /// an unordered_map lookup there dominated the perf profile.
    uint64_t efer_value_ = 0;
    static constexpr std::size_t kDecodeCacheCapacity = 1024; ///< 512 sets of two entries.
    using DecodeCache =
        base::SetAssocCache<uint64_t, CachedInsn, kDecodeCacheCapacity, 2, DecodeHash>;
    /// Decode cache (vaddr -> instruction); rebuilt by configure_caches().
    DecodeCache decode_cache_;
    /// Bitmap of executed physical pages, sized by configure_caches().
    /// Stores to marked pages invalidate the decode cache.
    std::vector<uint64_t> code_page_bits_;
    /// Marks @p paddr's page as an executed-from page.
    void mark_code_page(uint64_t paddr);

    /// Compiled block: executable page, entry and guest coverage.
    struct JitBlock {
        /// Blocks share 4 KiB pages (~1.3 KiB each); the shared_ptr keeps
        /// a page alive while any of its blocks lives in the table.
        std::shared_ptr<base::JitPage> page;
        std::size_t entry = 0;   ///< Block entry offset inside the page.
        uint64_t length = 0;     ///< Guest bytes covered by the block.
        unsigned insn_count = 0; ///< Guest instructions covered.
    };
    /// Compiles the block at @p rip (nullopt when nothing lowered).
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    std::optional<JitBlock> compile_block(uint64_t rip);

    void maybe_compile_block(uint64_t rip); // first-visit compile (env gated)
    [[nodiscard]] bool run_jit_block();     // true when a block ran

    std::unordered_map<uint64_t, JitBlock> jit_blocks_; // compiled by rip
    std::shared_ptr<base::JitPage> jit_page_;           // page being filled
    std::unordered_set<uint64_t> jit_failed_;           // rips that do not lower
    std::uint64_t jit_insn_count_ = 0;                  // coverage meter
    std::uint64_t code_lo_ = 0;                         // JIT range, 0 = no filter
    std::uint64_t code_hi_ = 0;
    bool jit_requested_ = false; // config switch (env bypasses)
    /// Invalidates the decode cache when @p paddr lands on a code page.
    void note_store(uint64_t paddr);
    std::array<uint8_t, 16> last_fault_bytes_{};
    unsigned last_fault_length_ = 0;
    /// Reads TSS.RSP0 for the current task: ltr's selector indexes the
    /// GDT, the descriptor gives the TSS base, offset 4 holds RSP0.
    [[nodiscard]] uint64_t kernel_stack_top();
    /// Raises #PF (vector 14) with its error code, or returns false when
    /// there is no IDT to deliver into (then the caller latches).
    bool raise_page_fault(uint64_t error);
    /// Queued interrupt delivery (batch 6c).
    uint8_t pending_vector_ = 0;
    bool interrupt_pending_ = false;
    /// Delivers the queued vector if IF is set and the machine runs.
    void deliver_interrupt();
    /// CR2 and the last #PF error code (read by the interrupt path).
    uint64_t cr2_ = 0, pf_error_ = 0;
    /// Software MMU (pass-through until the boot shim enables paging).
    Mmu mmu_{bus_};
    /// SSE2 register file: 16 XMM registers, two 64-bit halves each.
    std::array<std::array<uint64_t, 2>, 16> xmm_{};
    /// MXCSR control/status word (FXSAVE carries it across switches).
    uint32_t mxcsr_ = 0x1F80U;
    /// Deterministic RNG state (rdrand derives from the seed derivation
    /// of batch 8; a fixed splitmix64 seed until then).
    uint64_t rng_state_ = 0x9E3779B97F4A7C15ULL;
    /// Task/ldt selectors (0F 00 group; no descriptor semantics in v0).
    uint16_t tss_selector_ = 0;
    uint16_t ldt_selector_ = 0;
    /// Segment bases (FS/GS models; addressing arrives with the MMU).
    uint64_t fs_base_ = 0;
    uint64_t gs_base_ = 0, kernel_gs_base_ = 0;
    kernel::Tick icount_ = 0;
    std::vector<char> console_;
    std::function<void(uint64_t rip)> trace_;
    /// Attached interrupt line (wiring, not state: snapshots carry the
    /// line level in the device section, the pointer is re-attached).
    kernel::IrqLine* irq_line_ = nullptr;
    /// Receiving hook fired while the line is high (batch-6: injection).
    std::function<void()> irq_hook_;
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
