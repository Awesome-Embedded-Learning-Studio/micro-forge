#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "arch/arm/cortex_m3/cortex_m3_defs.hpp"
#include "arch/arm/cortex_m3/thumb32_fields.hpp"
#include "core/types.hpp"
#include "util/logger.hpp"
#include "util/perf_stats.hpp"
#include <bit>
#include <expected>

namespace micro_forge::cpu::arm::cortex_m3 {

using namespace thumb;

namespace {

const char* cpu_error_name(CPU::CPUError error) {
    switch (error) {
        case CPU::CPUError::IllegalInstruction:
            return "IllegalInstruction";
        case CPU::CPUError::DataAccessFault:
            return "DataAccessFault";
        case CPU::CPUError::InstructionFetchFault:
            return "InstructionFetchFault";
        case CPU::CPUError::InvalidPc:
            return "InvalidPc";
        case CPU::CPUError::ExceptionEntryFault:
            return "ExceptionEntryFault";
        case CPU::CPUError::ExceptionReturnFault:
            return "ExceptionReturnFault";
        case CPU::CPUError::NotRunning:
            return "NotRunning";
        case CPU::CPUError::RegisterIndexOverflow:
            return "RegisterIndexOverflow";
        case CPU::CPUError::FailedPollIntr:
            return "FailedPollIntr";
    }
    return "Unknown";
}

} // namespace

// ── Reset & ICore interface ──

CPU::CPUExpected<void> CortexM3CPU::reset() {
    regs_.reset();
    xpsr_ = PSR_T; // Thumb mode always on
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
    suspended_it_states_.clear();
    current_status_ = State::Halted;
    cycles_ = 0;
    return {};
}

CPU::CPUExpected<data_t> CortexM3CPU::register_value(std::size_t index) const {
    if (index >= 16) {
        return std::unexpected{CPUError::RegisterIndexOverflow};
    }
    const auto result = regs_.read(index);
    if (!result) {
        return std::unexpected{CPUError::RegisterIndexOverflow};
    }
    return *result;
}

CPU::CPUExpected<void> CortexM3CPU::set_register_value(std::size_t index,
                                                       data_t value) {
    if (index >= 16) {
        return std::unexpected{CPUError::RegisterIndexOverflow};
    }
    return write_reg(static_cast<uint8_t>(index), value);
}

CPU::CPUExpected<std::string_view>
CortexM3CPU::register_name(std::size_t idx) const {
    if (idx >= REGCNT) {
        return std::unexpected{CPUError::RegisterIndexOverflow};
    }
    return names[idx];
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

CPU::CPUExpected<CPU::ticks_t> CortexM3CPU::cycles() const {
    return cycles_;
}

// ── Internal register access ──

CPU::CPUExpected<addr_t> CortexM3CPU::read_pc_raw() const {
    auto result = regs_.read(15);
    if (!result) {
        return std::unexpected{CPUError::InvalidPc};
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
        return std::unexpected{CPUError::RegisterIndexOverflow};
    }
    return {};
}

// ── Flag helpers ──

void CortexM3CPU::update_nz(data_t result) {
    xpsr_ &= ~(PSR_N | PSR_Z);
    if (result & 0x80000000u) {
        xpsr_ |= PSR_N;
    }
    if (result == 0) {
        xpsr_ |= PSR_Z;
    }
}

void CortexM3CPU::update_flags(FlagPostOperation p, data_t a, data_t b,
                               data_t result) {
    xpsr_ &= ~(PSR_N | PSR_Z | PSR_C | PSR_V);
    if (result & 0x80000000u) {
        xpsr_ |= PSR_N;
    }
    if (result == 0) {
        xpsr_ |= PSR_Z;
    }

    if (p == FlagPostOperation::Add) {
        if (result < a) {
            xpsr_ |= PSR_C;
        }
        if (((a ^ ~b) & (a ^ result)) & 0x80000000u) {
            xpsr_ |= PSR_V;
        }
    } else {
        if (a >= b) {
            xpsr_ |= PSR_C;
        }
        if (((a ^ b) & (a ^ result)) & 0x80000000u) {
            xpsr_ |= PSR_V;
        }
    }
}

void CortexM3CPU::set_adc_flags(data_t a, data_t b, data_t cin, data_t result) {
    // ADC = a + b + cin. Carry-out is bit 32 of the full sum; V is signed
    // overflow (operands share sign, result flips it).
    xpsr_ &= ~(PSR_N | PSR_Z | PSR_C | PSR_V);
    if (result & 0x80000000u) {
        xpsr_ |= PSR_N;
    }
    if (result == 0) {
        xpsr_ |= PSR_Z;
    }
    uint64_t sum = static_cast<uint64_t>(a) + b + cin;
    if (sum >> 32) {
        xpsr_ |= PSR_C;
    }
    if (((~(a ^ b)) & (a ^ result)) & 0x80000000u) {
        xpsr_ |= PSR_V;
    }
}

void CortexM3CPU::set_sbc_flags(data_t a, data_t b, data_t cin, data_t result) {
    // SBC = a - b - !cin == a + ~b + cin (mod 2^32). The carry-out of that sum
    // is the ARM "no borrow" C flag; V is signed overflow of a - b.
    xpsr_ &= ~(PSR_N | PSR_Z | PSR_C | PSR_V);
    if (result & 0x80000000u) {
        xpsr_ |= PSR_N;
    }
    if (result == 0) {
        xpsr_ |= PSR_Z;
    }
    uint64_t sum = static_cast<uint64_t>(a) + (~b) + cin;
    if (sum >> 32) {
        xpsr_ |= PSR_C;
    }
    if (((a ^ b) & (a ^ result)) & 0x80000000u) {
        xpsr_ |= PSR_V;
    }
}

bool CortexM3CPU::condition_need_execute(uint8_t c) {
    bool N = xpsr_ & PSR_N;
    bool Z = xpsr_ & PSR_Z;
    bool C = xpsr_ & PSR_C;
    bool V = xpsr_ & PSR_V;
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
        auto w = bus_->write(sp, val, Width::Word);
        if (!w) {
            record_bus_fault(w.error(), sp, Width::Word);
            return std::unexpected{CPUError::DataAccessFault};
        }
    } else {
        record_bus_fault(BusError::InvalidDevice, sp, Width::Word);
        return std::unexpected{CPUError::DataAccessFault};
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
        auto r = bus_->read(sp, Width::Word);
        if (!r) {
            record_bus_fault(r.error(), sp, Width::Word);
            return std::unexpected{CPUError::DataAccessFault};
        }
        val = *r;
    } else {
        record_bus_fault(BusError::InvalidDevice, sp, Width::Word);
        return std::unexpected{CPUError::DataAccessFault};
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
        return std::unexpected{BusError::InvalidDevice};
    }
    // One HalfWord read replaces two Byte reads: instruction fetch only ever
    // hits FlatMemory (flash/SRAM), whose read() builds the little-endian
    // halfword via the same byte-shift loop, so the result is bit-identical to
    // the old `lo | (hi << 8)` reconstruction — but halves the Bus::read calls
    // per fetch (bit-band check + find_region + WeakPtr + virtual dispatch +
    // eager trace-arg eval each happen once, not twice).
    auto half = bus_->read(addr, Width::HalfWord);
    if (!half) {
        record_bus_fault(half.error(), addr, Width::HalfWord);
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
        current_status_ = State::Faulted;
        return std::unexpected{irq_res.error()};
    }
    if (active_priorities_.size() > depth_before) {
        // An exception entry consumed this step (covers first entry and
        // preemption of a running handler).
        cycles_++;
        return StepFlow::Return;
    }
    return StepFlow::Continue;
}

CPU::CPUExpected<void> CortexM3CPU::step_execute_one() {
    auto pc_res = read_pc_raw();
    if (!pc_res) {
        current_status_ = State::Faulted;
        return std::unexpected{pc_res.error()};
    }
    addr_t pc = *pc_res;
    pc_written_ = false; // an instruction may set this via write_reg(15)

    auto hw1_res = fetch16(pc);
    if (!hw1_res) {
        LOG_ERROR("fault", "PC=0x%08X opcode=0x%04X kind=%s detail=fetch", pc,
                  0, cpu_error_name(CPUError::InstructionFetchFault));
        if (!try_escalate_fault(CPUError::InstructionFetchFault, pc, 0, 0,
                                false)) {
            current_status_ = State::Faulted;
            return std::unexpected{CPUError::InstructionFetchFault};
        }
        cycles_++;
        return {};
    }
    uint16_t hw1 = *hw1_res;

    CPUExpected<void> exec_res;
    Expected<uint16_t> hw2_res;
    bool execute_instruction = true;
    if (it_condition_pos_ < it_conditions_.size()) {
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
            LOG_ERROR(
                "fault", "PC=0x%08X opcode=0x%04X%04X kind=%s detail=fetch-hw2",
                pc, hw1, 0, cpu_error_name(CPUError::InstructionFetchFault));
            if (!try_escalate_fault(CPUError::InstructionFetchFault, pc, hw1, 0,
                                    true)) {
                current_status_ = State::Faulted;
                return std::unexpected{CPUError::InstructionFetchFault};
            }
            cycles_++;
            return {};
        }
        exec_res = execute_instruction ? execute_32bit(hw1, *hw2_res)
                                       : CPUExpected<void>{};
        if (exec_res.has_value() && !pc_written_) {
            auto wr = write_reg(15, pc + 4);
            if (!wr) {
                return wr;
            }
        }
    } else {
        MF_PERF_INC(instr_16bit);
        exec_res =
            execute_instruction ? execute_16bit(hw1) : CPUExpected<void>{};
        if (exec_res.has_value() && !pc_written_) {
            auto wr = write_reg(15, pc + 2);
            if (!wr) {
                return wr;
            }
        }
    }

    if (!exec_res.has_value()) {
        bool is32 = is_32bit_prefix_instruction(hw1);
        uint16_t hw2_val = (is32 && hw2_res.has_value()) ? *hw2_res : 0;

        if (is32) {
            uint32_t insn32 = (static_cast<uint32_t>(hw1) << 16) | hw2_val;
            LOG_ERROR("fault",
                      "PC=0x%08X opcode=0x%08X hw1=0x%04X hw2=0x%04X kind=%s",
                      pc, insn32, hw1, hw2_val,
                      cpu_error_name(exec_res.error()));
        } else {
            LOG_ERROR("fault", "PC=0x%08X opcode=0x%04X kind=%s", pc, hw1,
                      cpu_error_name(exec_res.error()));
        }

        if (probe_mode_) {
            missing_opcodes_.emplace_back(pc, hw1, hw2_val);
            auto wr = write_reg(15, pc + (is32 ? 4 : 2));
            if (!wr) {
                return wr;
            }
        } else if (!try_escalate_fault(exec_res.error(), pc, hw1, hw2_val,
                                       is32)) {
            current_status_ = State::Faulted;
            return exec_res;
        }
    }

    cycles_++;
    return {};
}

CPU::CPUExpected<void> CortexM3CPU::step() {
    if (current_status_ != State::Running) {
        return std::unexpected{CPUError::NotRunning};
    }
    clear_pending_bus_fault();

    auto irq = step_take_interrupt();
    if (!irq) {
        return std::unexpected{irq.error()};
    }
    if (*irq == StepFlow::Return) {
        return {};
    }
    return step_execute_one();
}

} // namespace micro_forge::cpu::arm::cortex_m3
