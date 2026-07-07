#include "arch/toy/cpu.hpp"
#include "cpu/cpu.hpp"
#include <bit>
#include <expected>

namespace micro_forge::cpu {

// ── Construction & Setup ──

ToyCPU::ToyCPU(WeakPtr<memory::Bus> memory_bus) : memory_bus_(memory_bus) {
    intr_vec_.fill(0);
}

void ToyCPU::set_interrupt_vector(intr::intr_n_t irq_num, addr_t handler_addr) {
    if (irq_num < toy::INTR_CNT) {
        intr_vec_[irq_num] = handler_addr;
    }
}

// ── ICore interface ──

CPU::CPUExpected<void> ToyCPU::reset() {
    registers_.reset();
    pc_ = 0;
    zf_ = false;
    nf_ = false;
    current_status_ = State::Halted;
    cycles_ = 0;
    pending_irq_mask_ = 0;
    intr_vec_.fill(0);
    return {};
}

CPU::CPUExpected<data_t> ToyCPU::register_value(const std::size_t index) const {
    auto res = registers_.read(index);
    if (!res) {
        return std::unexpected{CPUError::RegisterIndexOverflow};
    }
    return *res;
}

CPU::CPUExpected<void> ToyCPU::set_register_value(const std::size_t index,
                                                  data_t value) {
    auto res = registers_.write(index, value);
    if (!res) {
        return std::unexpected{CPUError::RegisterIndexOverflow};
    }
    return {};
}

CPU::CPUExpected<std::string_view>
ToyCPU::register_name(std::size_t idx) const {
    static constexpr std::string_view names[] = {"R0", "R1", "R2", "R3",
                                                 "R4", "R5", "R6", "R7"};
    if (idx >= toy::RegCount) {
        return std::unexpected{CPUError::RegisterIndexOverflow};
    }
    return names[idx];
}

CPU::CPUExpected<void> ToyCPU::raise_irq(intr::intr_n_t irq_index) {
    if (irq_index < toy::INTR_CNT) {
        pending_irq_mask_ |= (1u << irq_index);
    }
    return {};
}

// ── Step pipeline ──

CPU::CPUExpected<void> ToyCPU::step() {
    if (current_status_ != State::Running) {
        return std::unexpected{CPUError::NotRunning};
    }

    auto result = poll_intr();
    if (!result) {
        current_status_ = CPU::State::Faulted;
        return std::unexpected{CPUError::FailedPollIntr};
    }

    auto ready_executed = fetch_instructions();
    if (!ready_executed) {
        current_status_ = CPU::State::Faulted;
        return std::unexpected{CPUError::InstructionFetchFault};
    }

    auto exe_res = execute_target_instructions(*ready_executed);
    if (!exe_res) {
        current_status_ = CPU::State::Faulted;
        return std::unexpected{CPUError::IllegalInstruction};
    }

    if (!exe_res->branched) {
        pc_ += 4;
    }
    cycles_++;

    return {};
}

// ── Internals ──

Expected<data_t> ToyCPU::fetch_instructions() {
    if (!memory_bus_) {
        return std::unexpected{BusError::InvalidDevice};
    }
    return memory_bus_->read(pc_, Width::Word);
}

void ToyCPU::update_flags(data_t result) {
    zf_ = (result == 0);
    nf_ = (static_cast<int32_t>(result) < 0);
}

CPU::CPUExpected<ExecInfo>
ToyCPU::execute_target_instructions(const data_t insn) {
    using namespace toy;
    ExecInfo info;

    switch (opcode(insn)) {
        case 0x0: // NOP
            break;

        case 0x1: { // ADD Rd, Rs, Rt
            auto sv = registers_.read(rs(insn));
            auto tv = registers_.read(rt(insn));
            if (!sv || !tv) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            data_t result = *sv + *tv;
            auto wr = registers_.write(rd(insn), result);
            if (!wr) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            update_flags(result);
            break;
        }

        case 0x2: { // SUB Rd, Rs, Rt
            auto sv = registers_.read(rs(insn));
            auto tv = registers_.read(rt(insn));
            if (!sv || !tv) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            data_t result = *sv - *tv;
            auto wr = registers_.write(rd(insn), result);
            if (!wr) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            update_flags(result);
            break;
        }

        case 0x3: { // AND Rd, Rs, Rt
            auto sv = registers_.read(rs(insn));
            auto tv = registers_.read(rt(insn));
            if (!sv || !tv) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            data_t result = *sv & *tv;
            auto wr = registers_.write(rd(insn), result);
            if (!wr) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            update_flags(result);
            break;
        }

        case 0x4: { // LDI Rd, imm15
            auto wr = registers_.write(rd(insn), imm15(insn));
            if (!wr) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            break;
        }

        case 0x5: { // LDW Rd, [Rs + imm5*4]
            auto sv = registers_.read(rs(insn));
            if (!sv) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            addr_t addr = *sv + imm5(insn) * 4;
            if (!memory_bus_) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            auto val = memory_bus_->read(addr, Width::Word);
            if (!val) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            auto wr = registers_.write(rd(insn), *val);
            if (!wr) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            break;
        }

        case 0x6: { // STW Rt, [Rs + imm5*4]
            auto sv = registers_.read(rs(insn));
            auto tv = registers_.read(rt(insn));
            if (!sv || !tv) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            addr_t addr = *sv + imm5(insn) * 4;
            if (!memory_bus_) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            auto res = memory_bus_->write(addr, *tv, Width::Word);
            if (!res) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            break;
        }

        case 0x7: { // BZ imm8
            if (zf_) {
                auto offset =
                    static_cast<int32_t>(static_cast<int8_t>(imm8(insn)));
                pc_ += offset * 4;
                info.branched = true;
            }
            break;
        }

        case 0x8: // JMP imm15
            pc_ = imm15(insn) * 4;
            info.branched = true;
            break;

        case 0x9: { // CALL imm15
            auto push_res = push_stack(pc_ + 4);
            if (!push_res) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            pc_ = imm15(insn) * 4;
            info.branched = true;
            break;
        }

        case 0xA: { // RET
            auto pop_res = pop_stack();
            if (!pop_res) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            pc_ = *pop_res;
            info.branched = true;
            break;
        }

        case 0xB: { // INT imm4
            auto res = raise_irq(imm4(insn));
            if (!res) {
                return std::unexpected{res.error()};
            }
            break;
        }

        case 0xC: // HALT
            current_status_ = State::Halted;
            break;

        default:
            return std::unexpected{CPUError::IllegalInstruction};
    }

    return info;
}

CPU::CPUExpected<void> ToyCPU::poll_intr() {
    if (pending_irq_mask_ == 0) {
        return {};
    }

    auto irq_num = static_cast<uint32_t>(std::countr_zero(pending_irq_mask_));
    pending_irq_mask_ &= ~(1u << irq_num);

    auto push_res = push_stack(pc_);
    if (!push_res) {
        return std::unexpected{CPUError::FailedPollIntr};
    }
    pc_ = intr_vec_[irq_num];
    return {};
}

Expected<void> ToyCPU::push_stack(data_t val) {
    auto sp_res = registers_.read(7);
    if (!sp_res) {
        return std::unexpected{BusError::PeripheralFault};
    }
    data_t sp = *sp_res - 4;
    if (memory_bus_) {
        auto write_res = memory_bus_->write(sp, val, Width::Word);
        if (!write_res) {
            return std::unexpected{write_res.error()};
        }
    }
    auto reg_res = registers_.write(7, sp);
    if (!reg_res) {
        return std::unexpected{BusError::PeripheralFault};
    }
    return {};
}

Expected<data_t> ToyCPU::pop_stack() {
    auto sp_res = registers_.read(7);
    if (!sp_res) {
        return std::unexpected{BusError::PeripheralFault};
    }
    data_t sp = *sp_res;
    data_t val = 0;
    if (memory_bus_) {
        auto read_res = memory_bus_->read(sp, Width::Word);
        if (!read_res) {
            return std::unexpected{read_res.error()};
        }
        val = *read_res;
    }
    auto reg_res = registers_.write(7, sp + 4);
    if (!reg_res) {
        return std::unexpected{BusError::PeripheralFault};
    }
    return val;
}

} // namespace micro_forge::cpu
