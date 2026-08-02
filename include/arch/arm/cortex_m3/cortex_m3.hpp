#pragma once

#include "autogen/arch_details.hpp"
#include "cpu/cpu.hpp"
#include "cpu/fault_record.hpp"
#include "cpu/regfile.hpp"
#include "cortex_m3_defs.hpp"
#include "memory/bus.hpp"
#include "periph/nvic.hpp"
#include "periph/scb.hpp"
#include "util/weak_ptr/weak_ptr.hpp"
#include "util/weak_ptr/weak_ptr_factory.hpp"
#include <cstdint>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace micro_forge::cpu {
namespace arm::cortex_m3 {
class CortexM3CPU : public CPU {
  public:
    // Raw observer pointer, not WeakPtr: the Bus is owned by Machine and
    // outlives the CPU (both are SoC members; Bus is constructed first). This
    // matches the existing nvic_/scb_ pattern — a WeakPtr here was a misuse
    // (the ownership tree is a clean unique_ptr, no cycle to break) and cost a
    // control-block deref + IsValid on every fetch/load/store.
    explicit CortexM3CPU(memory::Bus* bus) : bus_(bus) {}
    // CPU Interfaces
    CPUExpected<void> reset() override;
    CPUExpected<void> step() override;
    CPUExpected<State> state() const noexcept override {
        return current_status_;
    }
    CPUExpected<data_t> register_value(std::size_t index) const override;
    CPUExpected<void> set_register_value(std::size_t index,
                                         data_t value) override;
    CPUExpected<std::string_view> register_name(std::size_t idx) const override;
    CPUExpected<std::size_t> register_count() const override { return REGCNT; }
    CPUExpected<addr_t> pc() const override;
    CPUExpected<addr_t> set_pc(addr_t new_pc) override;
    CPUExpected<void> raise_irq(intr::intr_n_t irq_index) override;
    CPUExpected<ticks_t> cycles() const override;
    // P2 fast-forward: bump cycles_ without stepping (see cpu.hpp).
    CPUExpected<void> advance_cycles(ticks_t n) override {
        cycles_ += n;
        return {};
    }

    void launch() noexcept { current_status_ = State::Running; };

    // Raw observer pointers, not WeakPtr — intentional (C2 close-out). NVIC/SCB
    // live in Stm32f103Parts, a SoC member alongside the CPU, so they outlive
    // it and there is NO reference cycle to break (NVIC→CPU uses callbacks /
    // WeakPtr; the CPU→NVIC link is one-way). Used on the exception path
    // (raise_irq, priority lookup) — not per-instruction. Same reasoning as the
    // bus_ pointer above; see notes 041. A WeakPtr here would add a
    // control-block deref + IsValid with no cycle-prevention benefit.
    void set_nvic(periph::NvicPeripheral& nvic) { nvic_ = &nvic; }
    void set_scb(periph::ScbPeripheral& scb) { scb_ = &scb; }
    void set_vector_table_base(addr_t base) { vector_table_base_ = base; }
    void set_prigroup(uint8_t group) { prigroup_ = group & 0x7u; }
    bool in_handler_mode() const { return in_handler_mode_; }
    // P2.a WFI fast-forward: true while suspended on WFI. The coordinator
    // advances virtual time to the next exception to wake the CPU.
    bool is_sleeping() const noexcept override { return sleeping_; }
    // Read-only accessors for the status / mask / stack registers. They back
    // the structured introspection snapshot (introspection::read_introspection) consumed
    // by both the CLI JSON serializer and the GUI dashboard (milestone 04).
    // Inline by design — an out-of-line copy would still be off the fetch/
    // decode hot path, but keeping it header-inline avoids a TU/lookup cost
    // and matches the in_handler_mode() precedent.
    data_t xpsr() const noexcept { return xpsr_; }
    data_t primask() const noexcept { return primask_; }
    data_t basepri() const noexcept { return basepri_; }
    data_t faultmask() const noexcept { return faultmask_; }
    data_t control() const noexcept { return control_; }
    data_t msp() const noexcept { return msp_; }
    data_t psp() const noexcept { return psp_; }
    void sys_tick_irq() { pending_sys_tick_ = true; }

    // Probe mode: skip illegal instructions and log opcodes instead of halting
    void enable_probe_mode(bool on = true) { probe_mode_ = on; }

    // JIT translation cache (Step 3): {PC → decoded {handler, insn}}. On
    // hit, step_execute_one skips fetch16 + translate_16bit and dispatches
    // the cached handler directly. Opt-in (set_jit_enabled); off by default.
    void set_jit_enabled(bool on) noexcept {
        tcache_enabled_ = on;
        if (!on) {
            tcache_.clear();
        }
    }
    bool jit_enabled() const noexcept { return tcache_enabled_; }
    const auto& missing_opcodes() const { return missing_opcodes_; }
    void clear_missing_opcodes() { missing_opcodes_.clear(); }

    WeakPtr<CortexM3CPU> GetWeak() { return weak_factory_.GetWeakPtr(); }

    const std::optional<FaultRecord>& last_fault() const { return last_fault_; }

  private:
    // step() split into interrupt gating (step_take_interrupt) and the
    // fetch/IT/dispatch/fault pipeline (step_execute_one). StepFlow signals
    // whether take_interrupt consumed the step (exception entry).
    enum class StepFlow { Continue, Return };
    CPUExpected<StepFlow> step_take_interrupt();
    CPUExpected<void> step_execute_one();
    Expected<uint16_t> fetch16(addr_t addr);
    CPUExpected<void> execute_16bit(uint16_t insn);
    CPUExpected<void> execute_32bit(uint16_t hw1, uint16_t hw2);
    // JIT translate: decode a 16-bit Thumb instruction to its handler
    // (the same dispatch execute_16bit uses, but returns the handler instead
    // of calling it). nullptr = illegal opcode. Lets a translation cache
    // bind {PC → handler} once and skip the fetch+decode on hit.
    using Handler16 = CPUExpected<void> (CortexM3CPU::*)(uint16_t, uint16_t);
    Handler16 translate_16bit(uint16_t insn) const noexcept;
    // 16-bit Thumb family handlers — split out of execute_16bit so no single
    // translation unit exceeds the DIRECTIVES 700-line cap. Each returns the
    // result of its (prefix-/decode_key-matched) block; execute_16bit
    // dispatches. The extend/reverse prefix probes run *before* the decode_key
    // switch — that order is load-bearing (see OPEN GOTCHAS).
    CPUExpected<void> t16_extend(uint16_t insn, uint16_t);
    CPUExpected<void> t16_reverse(uint16_t insn, uint16_t);
    // Inline cases lifted out of execute_16bit's switch so the dispatch path
    // is a pure handler binding (JIT translate reuses it). Semantics identical
    // to the former inline blocks — only moved into named functions.
    CPUExpected<void> t16_cps(uint16_t insn, uint16_t);      // CPSIE/CPSID (0xB660)
    CPUExpected<void> t16_cbz(uint16_t insn, uint16_t);      // CBZ/CBNZ    (0xB100)
    CPUExpected<void> t16_adr(uint16_t insn, uint16_t);      // ADR         (0b10100)
    CPUExpected<void> t16_add_sp(uint16_t insn, uint16_t);   // ADD Rd,SP   (0b10101)
    CPUExpected<void> t16_b_cond(uint16_t insn, uint16_t);   // B<cond>     (0b11010/11011)
    CPUExpected<void> t16_b(uint16_t insn, uint16_t);        // B           (0b11100)
    CPUExpected<void> t16_shift_imm(uint16_t insn, uint16_t);
    CPUExpected<void> t16_addsub_reg3(uint16_t insn, uint16_t);
    CPUExpected<void> t16_imm8_dataops(uint16_t insn, uint16_t);
    CPUExpected<void> t16_special_bx(uint16_t insn, uint16_t);
    CPUExpected<void> t16_dataproc_reg(uint16_t insn, uint16_t);
    CPUExpected<void> t16_ldr_literal(uint16_t insn, uint16_t);
    CPUExpected<void> t16_loadstore_reg_offset(uint16_t insn, uint16_t);
    CPUExpected<void> t16_loadstore_imm_offset(uint16_t insn, uint16_t);
    CPUExpected<void> t16_loadstore_sp_rel(uint16_t insn, uint16_t);
    CPUExpected<void> t16_push(uint16_t insn, uint16_t);
    CPUExpected<void> t16_pop(uint16_t insn, uint16_t);
    CPUExpected<void> t16_stm_ldm(uint16_t insn, uint16_t);
    // 32-bit Thumb-2 family handlers — split out of execute_32bit so no single
    // translation unit exceeds the DIRECTIVES 700-line cap. Each returns the
    // result of its (already mask-matched) block; execute_32bit dispatches.
    CPUExpected<void> t32_addsub_plain_imm(uint16_t hw1, uint16_t hw2);
    CPUExpected<void> t32_dataproc_imm(uint16_t hw1, uint16_t hw2);
    CPUExpected<void> t32_dataproc_reg(uint16_t hw1, uint16_t hw2);
    // Shared apply for the modified-immediate and shifted-register data-proc
    // forms — identical op table / flags / Rd=15 handling; they differ only in
    // the second operand (operand_b). op = hw1[8:5]. Defined inline so the two
    // call sites fold it away (an out-of-line copy regressed gpio/uart ~10%).
    CPUExpected<void> t32_dataproc_apply(uint8_t op, bool s_bit, uint8_t rn,
                                         uint8_t rd, uint32_t rn_val,
                                         uint32_t operand_b) {
        uint32_t result;
        switch (op) {
            case 0: result = rn_val & operand_b; break;            // AND/TST
            case 1: result = rn_val & ~operand_b; break;           // BIC
            case 2: result = (rn == 15) ? operand_b : (rn_val | operand_b); break;   // ORR/MOV
            case 3: result = (rn == 15) ? ~operand_b : (rn_val | ~operand_b); break; // ORN/MVN
            case 4: result = rn_val ^ operand_b; break;            // EOR/TEQ
            case 8: result = rn_val + operand_b; break;            // ADD/CMN
            case 10: result = rn_val + operand_b + ((xpsr_ & PSR_C) ? 1u : 0u); break; // ADC
            case 11: result = rn_val - operand_b - ((xpsr_ & PSR_C) ? 0u : 1u); break; // SBC
            case 13: result = rn_val - operand_b; break;           // SUB/CMP
            case 14: result = operand_b - rn_val; break;           // RSB
            default: return std::unexpected{CPUError::IllegalInstruction};
        }
        if (s_bit) {
            data_t cin = (xpsr_ & PSR_C) ? 1u : 0u;
            if (op == 8) {
                update_flags(FlagPostOperation::Add, rn_val, operand_b, result);
            } else if (op == 10) {
                set_adc_flags(rn_val, operand_b, cin, result);
            } else if (op == 11) {
                set_sbc_flags(rn_val, operand_b, cin, result);
            } else if (op == 13) {
                update_flags(FlagPostOperation::Sub, rn_val, operand_b, result);
            } else if (op == 14) {
                update_flags(FlagPostOperation::Sub, operand_b, rn_val, result);
            } else {
                update_nz(result);
            }
        }
        // CMP/CMN/TST/TEQ: S=1, Rd=15 → flags only, no register write.
        if (s_bit && rd == 15) {
            return {};
        }
        return wr(rd, result);
    }
    CPUExpected<void> t32_misc_reverse(uint16_t hw1, uint16_t hw2);
    CPUExpected<void> t32_ssat_usat(uint16_t hw1, uint16_t hw2);
    CPUExpected<void> t32_shift_reg(uint16_t hw1, uint16_t hw2);
    CPUExpected<void> t32_loadstore_single(uint16_t hw1, uint16_t hw2);
    CPUExpected<void> t32_ldrex_strex(uint16_t hw1, uint16_t hw2);
    CPUExpected<void> t32_tbb_tbh(uint16_t hw1, uint16_t hw2);
    CPUExpected<void> t32_strd_ldrd(uint16_t hw1, uint16_t hw2);
    CPUExpected<void> t32_stm_ldm(uint16_t hw1, uint16_t hw2);
    // Operand helpers shared across the 32-bit handlers (promoted from the
    // execute_32bit-local lambdas so the split-out handlers can use them).
    data_t rr(uint8_t idx);
    CPUExpected<void> wr(uint8_t idx, data_t val);
    CPUExpected<data_t> br(addr_t addr, Width w);
    CPUExpected<void> bw(addr_t addr, data_t val, Width w);
    CPU::CPUExpected<addr_t> read_pc_raw() const;
    CPU::CPUExpected<void> write_reg(uint8_t index, data_t value);

    // Unified PC write — detects EXC_RETURN in handler mode
    CPUExpected<void> write_pc(data_t value);

    /* Algorithm Helpers */
    void update_nz(data_t result);
    enum class FlagPostOperation { Add, Sub };
    void update_flags(FlagPostOperation p, data_t a, data_t b, data_t result);

    // ADC/SBC flag updates with an explicit carry-in. Unlike update_flags(Add/
    // Sub), C and V are derived from a 64-bit sum so the carry-in folds in
    // correctly even when the second operand is 0xFFFFFFFF (folding Cin into
    // the operand before a plain add/sub would wrap there and mis-report C).
    void set_adc_flags(data_t a, data_t b, data_t cin, data_t result);
    void set_sbc_flags(data_t a, data_t b, data_t cin, data_t result);

    /* If we get false, then we need to jump */
    bool condition_need_execute(uint8_t command);
    CPUExpected<void> push_stack(data_t val);
    CPUExpected<data_t> pop_stack();

    // Interrupt handling
    CPUExpected<void> check_and_handle_interrupt();
    CPUExpected<void> exception_entry_common(addr_t vector_addr,
                                             uint8_t new_priority);
    CPUExpected<void> interrupt_entry(uint8_t irq_n);
    CPUExpected<void> interrupt_entry_system(uint8_t exception_num);
    CPUExpected<void> interrupt_return(data_t exc_return);
    CPUExpected<void> trigger_hardfault();

    // Priority helpers
    // STM32F103 implements 4 priority bits. preempt_priority() reduces a raw
    // priority byte to its preemption-group portion per AIRCR.PRIGROUP.
    uint8_t preempt_priority(uint8_t raw) const;
    // Priority of a system exception; reads SCB SHPR when wired, else 0xFF.
    uint8_t system_exception_priority(uint8_t exc_num) const;
    bool try_escalate_fault(CPUError kind, addr_t pc, uint16_t hw1,
                            uint16_t hw2, bool is32);

  private:
    memory::Bus* bus_ = nullptr;
    reg::Registers<REGCNT> regs_;
    State current_status_ = State::Halted;
    std::optional<FaultRecord> last_fault_;

    void record_fault(CPUError kind, addr_t pc, uint16_t hw1, uint16_t hw2,
                      bool is32) {
        last_fault_.emplace();
        auto& r = *last_fault_;
        r.pc = pc;
        r.lr = regs_.read(14).value_or(0);
        r.sp = regs_.read(13).value_or(0);
        r.xpsr = xpsr_;
        r.opcode16 = hw1;
        r.opcode16_2 = hw2;
        r.is_32bit = is32;
        r.kind = kind;
        r.bus_error = pending_bus_error_;
        r.access_addr = pending_access_addr_;
        r.access_width = pending_access_width_;
        clear_pending_bus_fault();
    }

    void record_bus_fault(BusError error, addr_t addr, Width width) {
        pending_bus_error_ = error;
        pending_access_addr_ = addr;
        pending_access_width_ = width;
    }

    void clear_pending_bus_fault() {
        pending_bus_error_.reset();
        pending_access_addr_.reset();
        pending_access_width_.reset();
    }

    data_t xpsr_ = 0;    // CPU Status Flags as XPSR Register
    data_t primask_ = 0; // Intr Mask Registers
    data_t basepri_ = 0;
    data_t faultmask_ = 0;
    data_t control_ = 0;
    data_t msp_ = 0;
    data_t psp_ = 0;
    ticks_t cycles_ = 0;
    bool sleeping_ = false; // WFI: suspend fetch until an exception arrives

    // Interrupt state
    periph::NvicPeripheral* nvic_ = nullptr;
    periph::ScbPeripheral* scb_ = nullptr;
    bool in_handler_mode_ = false;
    uint8_t current_priority_ = 0xFF;
    uint8_t prigroup_ = 0;
    // Saved active priorities for nested exception return. Thread mode's 0xFF
    // is pushed on first entry and restored on return to thread mode.
    std::vector<uint8_t> active_priorities_;
    addr_t vector_table_base_ = 0x08000000;
    bool pending_sys_tick_ = false;
    bool pc_written_ = false; // write_reg(15) sets it; step uses it to tell an
                              // explicit branch from sequential fall-through

    // Probe mode state
    bool probe_mode_ = false;
    // JIT translation cache (16-bit Thumb): PC → {handler, insn}.
    struct CachedInsn {
        Handler16 handler;
        uint16_t insn;
    };
    std::unordered_map<addr_t, CachedInsn> tcache_;
    bool tcache_enabled_ = false;
    std::vector<std::tuple<addr_t, uint16_t, uint16_t>> missing_opcodes_;
    struct ItState {
        std::vector<uint8_t> conditions;
        size_t condition_pos = 0;
    };
    std::vector<uint8_t> it_conditions_;
    size_t it_condition_pos_ = 0;
    // ITSTATE is part of xPSR on real Cortex-M hardware and is therefore
    // preserved across exception entry/return.  The emulator represents it
    // separately, so keep an explicit stack for nested exceptions.
    std::vector<ItState> suspended_it_states_;
    std::optional<BusError> pending_bus_error_;
    std::optional<addr_t> pending_access_addr_;
    std::optional<Width> pending_access_width_;

    WeakPtrFactory<CortexM3CPU> weak_factory_{this};
};
} // namespace arm::cortex_m3
} // namespace micro_forge::cpu
