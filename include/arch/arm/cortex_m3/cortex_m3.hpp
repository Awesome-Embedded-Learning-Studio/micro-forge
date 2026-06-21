#pragma once

#include "autogen/arch_details.hpp"
#include "cpu/cpu.hpp"
#include "cpu/fault_record.hpp"
#include "cpu/regfile.hpp"
#include "def.h"
#include "memory/bus.hpp"
#include "periph/nvic.hpp"
#include "periph/scb.hpp"
#include "util/weak_ptr/weak_ptr.h"
#include "util/weak_ptr/weak_ptr_factory.h"
#include <cstdint>
#include <tuple>
#include <vector>

namespace micro_forge::cpu {
namespace arm::cortex_m3 {
class CortexM3CPU : public CPU {
  public:
    explicit CortexM3CPU(WeakPtr<memory::Bus> bus) : bus_(bus) {}
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

    void launch() noexcept { current_status_ = State::Running; };

    WeakPtr<memory::Bus> memory_bus() { return bus_; }

    void set_nvic(periph::NvicPeripheral& nvic) { nvic_ = &nvic; }
    void set_scb(periph::ScbPeripheral& scb) { scb_ = &scb; }
    void set_vector_table_base(addr_t base) { vector_table_base_ = base; }
    void set_prigroup(uint8_t group) { prigroup_ = group & 0x7u; }
    bool in_handler_mode() const { return in_handler_mode_; }
    void sys_tick_irq() { pending_sys_tick_ = true; }

    // Probe mode: skip illegal instructions and log opcodes instead of halting
    void enable_probe_mode(bool on = true) { probe_mode_ = on; }
    const auto& missing_opcodes() const { return missing_opcodes_; }
    void clear_missing_opcodes() { missing_opcodes_.clear(); }

    WeakPtr<CortexM3CPU> GetWeak() { return weak_factory_.GetWeakPtr(); }

    const std::optional<FaultRecord>& last_fault() const { return last_fault_; }

  private:
    Expected<uint16_t> fetch16(addr_t addr);
    CPUExpected<void> execute_16bit(uint16_t insn);
    CPUExpected<void> execute_32bit(uint16_t hw1, uint16_t hw2);
    // 32-bit Thumb-2 family handlers — split out of execute_32bit so no single
    // translation unit exceeds the DIRECTIVES 700-line cap. Each returns the
    // result of its (already mask-matched) block; execute_32bit dispatches.
    CPUExpected<void> t32_addsub_plain_imm(uint16_t hw1, uint16_t hw2);
    CPUExpected<void> t32_dataproc_imm(uint16_t hw1, uint16_t hw2);
    CPUExpected<void> t32_dataproc_reg(uint16_t hw1, uint16_t hw2);
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
    WeakPtr<memory::Bus> bus_;
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
    std::vector<std::tuple<addr_t, uint16_t, uint16_t>> missing_opcodes_;
    std::vector<uint8_t> it_conditions_;
    size_t it_condition_pos_ = 0;
    std::optional<BusError> pending_bus_error_;
    std::optional<addr_t> pending_access_addr_;
    std::optional<Width> pending_access_width_;

    WeakPtrFactory<CortexM3CPU> weak_factory_{this};
};
} // namespace arm::cortex_m3
} // namespace micro_forge::cpu
