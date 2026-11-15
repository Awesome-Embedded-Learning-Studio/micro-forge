#include "micro_forge/arch/arm/cortex_m3/cortex_m3.hpp"
#include "micro_forge/arch/arm/cortex_m3/cortex_m3_defs.hpp"
#include "micro_forge/arch/arm/cortex_m3/thumb32_fields.hpp"
#include "micro_forge/arch/base/perf_stats.hpp"
#include "micro_forge/device/base/types.hpp"
#include <bit>
#include <expected>

namespace micro_forge::arch::arm::cortex_m3 {

using namespace thumb;

// ── Reset & ICore interface ──

CPU::CPUExpected<void> CortexM3CPU::reset() {
    regs_.reset();
    xpsr_ = kPsrT; // Thumb mode always on
    primask_ = 0;
    basepri_ = 0;
    faultmask_ = 0;
    control_ = 0;
    msp_ = 0;
    psp_ = 0;
    prigroup_ = 0;
    in_handler_mode_ = false;
    current_priority_ = 0xFF;
    pending_sys_tick_ = false;
    active_priorities_.clear();
    it_conditions_.clear();
    it_condition_pos_ = 0;
    executing_it_ = false;
    suspended_it_states_.clear();
    current_status_ = State::kHalted;
    cycles_ = 0;
    sleeping_ = false;
    pc_written_ = false;
    last_fault_.reset();
    clear_pending_bus_fault();
    tcache_.clear();
    missing_opcodes_.clear();
    return {};
}

CPU::CPUExpected<data_t> CortexM3CPU::register_value(std::size_t index) const {
    if (index >= 16) {
        return std::unexpected{CPUError::kRegisterIndexOverflow};
    }
    const auto result = regs_.read(index);
    if (!result) {
        return std::unexpected{CPUError::kRegisterIndexOverflow};
    }
    return *result;
}

CPU::CPUExpected<void> CortexM3CPU::set_register_value(std::size_t index, data_t value) {
    if (index >= 16) {
        return std::unexpected{CPUError::kRegisterIndexOverflow};
    }
    return write_reg(static_cast<uint8_t>(index), value);
}

CPU::CPUExpected<std::string_view> CortexM3CPU::register_name(std::size_t idx) const {
    if (idx >= kRegisterCount) {
        return std::unexpected{CPUError::kRegisterIndexOverflow};
    }
    return kRegisterNames[idx];
}

CPU::CPUExpected<addr_t> CortexM3CPU::pc() const {
    auto v = regs_.read(15);
    return v ? *v : 0;
}

CPU::CPUExpected<addr_t> CortexM3CPU::set_pc(addr_t new_pc) {
    const auto res = write_reg(15, new_pc);
    if (!res) {
        return std::unexpected{res.error()};
    }
    return new_pc;
}

CPU::CPUExpected<void> CortexM3CPU::raise_irq(intr::intr_n_t irq) {
    // External peripheral → NVIC pending injection. This is the single channel
    // every MMIO IRQ (TIM/USART/EXTI…) funnels through; SysTick bypasses it via
    // sys_tick_irq() (system exception 15, not an NVIC line).
    if (nvic_) {
        nvic_->set_pending(static_cast<uint8_t>(irq));
    }
    return {};
}

CPU::CPUExpected<CPU::Ticks> CortexM3CPU::cycles() const {
    return cycles_;
}

// ── Internal register access ──

CPU::CPUExpected<addr_t> CortexM3CPU::read_pc_raw() const {
    auto result = regs_.read(15);
    if (!result) {
        return std::unexpected{CPUError::kInvalidPc};
    }
    return *result;
}

CPU::CPUExpected<void> CortexM3CPU::write_reg(uint8_t index, data_t value) {
    if (index == 13) {
        value &= ~0x3u; // SP: low 2 bits forced to 0
        if (!in_handler_mode_ && (control_ & 0x2u)) {
            psp_ = value;
        } else {
            msp_ = value;
        }
    }
    if (index == 15) {
        pc_written_ = true; // control flow changed this step (branch/return)
    }
    auto result = regs_.write(index, value);
    if (!result) {
        return std::unexpected{CPUError::kRegisterIndexOverflow};
    }
    return {};
}

// ── Flag helpers ──

void CortexM3CPU::update_nz(data_t result) {
    xpsr_ &= ~(kPsrN | kPsrZ);
    if (result & 0x80000000u) {
        xpsr_ |= kPsrN;
    }
    if (result == 0) {
        xpsr_ |= kPsrZ;
    }
}

void CortexM3CPU::update_flags(FlagPostOperation p, data_t a, data_t b, data_t result) {
    xpsr_ &= ~(kPsrN | kPsrZ | kPsrC | kPsrV);
    if (result & 0x80000000u) {
        xpsr_ |= kPsrN;
    }
    if (result == 0) {
        xpsr_ |= kPsrZ;
    }

    if (p == FlagPostOperation::kAdd) {
        if (result < a) {
            xpsr_ |= kPsrC;
        }
        if (((a ^ ~b) & (a ^ result)) & 0x80000000u) {
            xpsr_ |= kPsrV;
        }
    } else {
        if (a >= b) {
            xpsr_ |= kPsrC;
        }
        if (((a ^ b) & (a ^ result)) & 0x80000000u) {
            xpsr_ |= kPsrV;
        }
    }
}

void CortexM3CPU::set_adc_flags(data_t a, data_t b, data_t cin, data_t result) {
    // ADC = a + b + cin. Carry-out is bit 32 of the full sum; V is signed
    // overflow (operands share sign, result flips it).
    xpsr_ &= ~(kPsrN | kPsrZ | kPsrC | kPsrV);
    if (result & 0x80000000u) {
        xpsr_ |= kPsrN;
    }
    if (result == 0) {
        xpsr_ |= kPsrZ;
    }
    uint64_t sum = static_cast<uint64_t>(a) + b + cin;
    if (sum >> 32) {
        xpsr_ |= kPsrC;
    }
    if (((~(a ^ b)) & (a ^ result)) & 0x80000000u) {
        xpsr_ |= kPsrV;
    }
}

void CortexM3CPU::set_sbc_flags(data_t a, data_t b, data_t cin, data_t result) {
    // SBC = a - b - !cin == a + ~b + cin (mod 2^32). The carry-out of that sum
    // is the ARM "no borrow" C flag; V is signed overflow of a - b.
    xpsr_ &= ~(kPsrN | kPsrZ | kPsrC | kPsrV);
    if (result & 0x80000000u) {
        xpsr_ |= kPsrN;
    }
    if (result == 0) {
        xpsr_ |= kPsrZ;
    }
    uint64_t sum = static_cast<uint64_t>(a) + (~b) + cin;
    if (sum >> 32) {
        xpsr_ |= kPsrC;
    }
    if (((a ^ b) & (a ^ result)) & 0x80000000u) {
        xpsr_ |= kPsrV;
    }
}

bool CortexM3CPU::condition_need_execute(uint8_t c) {
    bool N = xpsr_ & kPsrN;
    bool Z = xpsr_ & kPsrZ;
    bool C = xpsr_ & kPsrC;
    bool V = xpsr_ & kPsrV;
    switch (c) {
    case 0x0:
        return Z; // EQ
    case 0x1:
        return !Z; // NE
    case 0x2:
        return C; // CS/HS
    case 0x3:
        return !C; // CC/LO
    case 0x4:
        return N; // MI
    case 0x5:
        return !N; // PL
    case 0x6:
        return V; // VS
    case 0x7:
        return !V; // VC
    case 0x8:
        return C && !Z; // HI
    case 0x9:
        return !C || Z; // LS
    case 0xA:
        return N == V; // GE
    case 0xB:
        return N != V; // LT
    case 0xC:
        return !Z && (N == V); // GT
    case 0xD:
        return Z || (N != V); // LE
    case 0xE:
        return true; // AL
    default:
        return false; // 0xF
    }
}

// ── Priority helpers ──

uint8_t CortexM3CPU::preempt_priority(uint8_t raw) const {
    // STM32F103 implements 4 priority bits. The preemption field width is
    // min(4, 7 - PRIGROUP) bits, taken from the MSB side of the 8-bit field.
    uint8_t g = prigroup_ & 0x7u;
    uint8_t preempt_bits = (7u - g > 4u) ? 4u : static_cast<uint8_t>(7u - g);
    return static_cast<uint8_t>(raw >> (8u - preempt_bits));
}

uint8_t CortexM3CPU::system_exception_priority(uint8_t exc_num) const {
    // HardFault(3) has a fixed priority higher than any maskable exception;
    // represent it as the smallest value (highest priority).
    if (exc_num == 3) {
        return 0;
    }
    if (scb_) {
        return scb_->system_exception_priority(exc_num);
    }
    return 0xFF;
}

// ── Stack operations ──

CPU::CPUExpected<void> CortexM3CPU::push_stack(data_t val) {
    auto sp_res = regs_.read(13);
    data_t sp = *sp_res - 4;
    if (bus_) {
        auto w = bus_->write(sp, val, Width::kWord);
        if (!w) {
            record_bus_fault(w.error(), sp, Width::kWord);
            return std::unexpected{CPUError::kDataAccessFault};
        }
    } else {
        record_bus_fault(BusError::kInvalidDevice, sp, Width::kWord);
        return std::unexpected{CPUError::kDataAccessFault};
    }
    // Route through write_reg(13) so the MSP/PSP shadow stays in sync with R13
    // (the active-SP invariant). A bare regs_.write(13) would desync the
    // shadow.
    return write_reg(13, sp);
}

CPU::CPUExpected<data_t> CortexM3CPU::pop_stack() {
    data_t sp = *regs_.read(13);
    data_t val = 0;
    if (bus_) {
        auto r = bus_->read(sp, Width::kWord);
        if (!r) {
            record_bus_fault(r.error(), sp, Width::kWord);
            return std::unexpected{CPUError::kDataAccessFault};
        }
        val = *r;
    } else {
        record_bus_fault(BusError::kInvalidDevice, sp, Width::kWord);
        return std::unexpected{CPUError::kDataAccessFault};
    }
    // Route through write_reg(13) to keep the MSP/PSP shadow in sync with R13.
    auto wr = write_reg(13, sp + 4);
    if (!wr) {
        return std::unexpected{wr.error()};
    }
    return val;
}

// ── Fetch ──

Expected<uint16_t> CortexM3CPU::fetch16(addr_t addr) {
    MF_PERF_INC(fetches);
    if (!bus_) {
        record_bus_fault(BusError::kInvalidDevice, addr, Width::kHalfWord);
        return std::unexpected{BusError::kInvalidDevice};
    }
    // Fetch through the shared bus adapter, including MMIO and mapping faults.
    auto half = bus_->read(addr, Width::kHalfWord);
    if (!half) {
        record_bus_fault(half.error(), addr, Width::kHalfWord);
        return std::unexpected{half.error()};
    }
    return static_cast<uint16_t>(*half);
}

// ── Step ──

CPU::CPUExpected<CortexM3CPU::StepFlow> CortexM3CPU::step_take_interrupt() {
    // Check for pending interrupts before instruction fetch. If an interrupt is
    // taken, this step is consumed by the entry sequence (stacking + vector
    // fetch); handler instructions start executing next step.
    const size_t depth_before = active_priorities_.size();
    auto irq_res = check_and_handle_interrupt();
    if (!irq_res) {
        current_status_ = State::kFaulted;
        return std::unexpected{irq_res.error()};
    }
    if (active_priorities_.size() > depth_before) {
        // An exception entry consumed this step (covers first entry and
        // preemption of a running handler). Entering a handler also wakes
        // the CPU from WFI.
        sleeping_ = false;
        cycles_++;
        return StepFlow::kReturn;
    }
    return StepFlow::kContinue;
}

CPU::CPUExpected<void> CortexM3CPU::step_execute_one() {
    auto pc_res = read_pc_raw();
    if (!pc_res) {
        current_status_ = State::kFaulted;
        return std::unexpected{pc_res.error()};
    }
    addr_t pc = *pc_res;
    pc_written_ = false; // an instruction may set this via write_reg(15)

    auto hw1_res = fetch16(pc);
    if (!hw1_res) {
        if (!try_escalate_fault(CPUError::kInstructionFetchFault, pc, 0, 0, false)) {
            current_status_ = State::kFaulted;
            return std::unexpected{CPUError::kInstructionFetchFault};
        }
        cycles_++;
        return {};
    }
    uint16_t hw1 = *hw1_res;

    CPUExpected<void> exec_res;
    Expected<uint16_t> hw2_res;
    bool execute_instruction = true;
    executing_it_ = it_condition_pos_ < it_conditions_.size();
    if (executing_it_) {
        uint8_t it_cond = it_conditions_[it_condition_pos_++];
        if (it_condition_pos_ >= it_conditions_.size()) {
            it_conditions_.clear();
            it_condition_pos_ = 0;
        }
        execute_instruction = condition_need_execute(it_cond);
    }

    if (is_32bit_prefix_instruction(hw1)) {
        MF_PERF_INC(instr_32bit);
        hw2_res = fetch16(pc + 2);
        if (!hw2_res) {
            if (!try_escalate_fault(CPUError::kInstructionFetchFault, pc, hw1, 0, true)) {
                current_status_ = State::kFaulted;
                return std::unexpected{CPUError::kInstructionFetchFault};
            }
            cycles_++;
            return {};
        }
        exec_res = execute_instruction ? execute_32bit(hw1, *hw2_res) : CPUExpected<void>{};
        if (exec_res.has_value() && !pc_written_) {
            auto wr = write_reg(15, pc + 4);
            if (!wr) {
                return wr;
            }
        }

    } else {
        MF_PERF_INC(instr_16bit);
        exec_res = execute_instruction ? execute_16bit(hw1) : CPUExpected<void>{};
        if (exec_res.has_value() && !pc_written_) {
            auto wr = write_reg(15, pc + 2);
            if (!wr) {
                return wr;
            }
        }
    }

    if (!exec_res.has_value()) {
        const bool is32 = is_32bit_prefix_instruction(hw1);
        const uint16_t hw2_val = (is32 && hw2_res.has_value()) ? *hw2_res : 0;

        if (probe_mode_) {
            missing_opcodes_.emplace_back(pc, hw1, hw2_val);
            auto wr = write_reg(15, pc + (is32 ? 4 : 2));
            if (!wr) {
                return wr;
            }
        } else if (!try_escalate_fault(exec_res.error(), pc, hw1, hw2_val, is32)) {
            current_status_ = State::kFaulted;
            return exec_res;
        }
    }

    cycles_++;
    return {};
}

CortexM3CPU::Handler16 CortexM3CPU::cached_handler(addr_t pc, uint16_t hw1, uint16_t hw2,
                                                   bool is32) {
    // Always fetch first. RAM writes, DMA, aliases, and loader changes can all
    // replace instructions without notifying this CPU. Validate both halfwords
    // and instruction length before reusing a decoded handler.
    if (tcache_enabled_) {
        const auto it = tcache_.find(pc);
        if (it != tcache_.end()) {
            const auto& cached = it->second;
            if (cached.hw1 == hw1 && cached.hw2 == hw2 && cached.is_32bit == is32) {
                return cached.handler;
            }
            tcache_.erase(it);
        }
    }
    const auto handler = is32 ? translate_32bit(hw1, hw2) : translate_16bit(hw1);
    if (tcache_enabled_ && handler) {
        tcache_[pc] = {handler, hw1, hw2, is32};
    }
    return handler;
}

CPU::CPUExpected<void> CortexM3CPU::step() {
    if (current_status_ != State::kRunning) {
        return std::unexpected{CPUError::kNotRunning};
    }
    clear_pending_bus_fault();

    auto irq = step_take_interrupt();
    if (!irq) {
        return std::unexpected{irq.error()};
    }
    if (*irq == StepFlow::kReturn) {
        return {};
    }
    if (sleeping_) {
        // WFI: no exception is pending, so stay suspended — don't fetch. The
        // coordinator advances virtual time to the next IRQ; without it, we
        // burn one cycle per step until a timer fires.
        cycles_++;
        return {};
    }
    return step_execute_one();
}

} // namespace micro_forge::arch::arm::cortex_m3
