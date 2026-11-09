/**
 * @file fast_dispatch.cpp
 * @brief Prevalidated common instruction forms for interpreter cache hits.
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/cpu.hpp"

namespace micro_forge::arch::x86_64 {
namespace {

bool gpr(const Operand& op, unsigned width) {
    return (op.kind == Operand::Kind::kReg || op.kind == Operand::Kind::kImplicit) &&
           static_cast<unsigned>(op.reg) < 16U && op.size == width;
}

ExecOp classify(const DecodedInsn& insn) {
    const auto& a = insn.ops[0];
    const auto& b = insn.ops[1];
    if (insn.mnemonic == Mnemonic::kMov || insn.mnemonic == Mnemonic::kMovabs ||
        insn.mnemonic == Mnemonic::kMovzx) {
        if (gpr(a, 8) || gpr(a, 4)) {
            if (b.kind == Operand::Kind::kImm) {
                return a.size == 8 ? ExecOp::kImm64 : ExecOp::kImm32;
            }
            if (b.size == a.size && gpr(b, b.size)) {
                return a.size == 8 ? ExecOp::kMov64 : ExecOp::kMov32;
            }
            // MOVZX and same-width MOV both write zero-extended memory
            // values; a 32-bit destination also clears the upper GPR half.
            if (b.kind == Operand::Kind::kMem) {
                switch (b.size) {
                case 8:
                    return ExecOp::kLoad64;
                case 4:
                    return ExecOp::kLoad32;
                case 2:
                    return ExecOp::kLoad16;
                case 1:
                    return ExecOp::kLoad8;
                default:
                    break;
                }
            }
        }
        if (a.kind == Operand::Kind::kMem) {
            if (b.kind == Operand::Kind::kImm &&
                (a.size == 1 || a.size == 2 || a.size == 4 || a.size == 8)) {
                return ExecOp::kStoreImm;
            }
            if (gpr(b, a.size)) {
                switch (a.size) {
                case 8:
                    return ExecOp::kStore64;
                case 4:
                    return ExecOp::kStore32;
                case 2:
                    return ExecOp::kStore16;
                case 1:
                    if (insn.rex_present || static_cast<unsigned>(b.reg) < 4U ||
                        static_cast<unsigned>(b.reg) > 7U) {
                        return ExecOp::kStore8;
                    }
                    break; // AH/CH/DH/BH use the full alias-aware executor.
                default:
                    break;
                }
            }
        }
    }
    if (insn.mnemonic == Mnemonic::kLea && b.kind == Operand::Kind::kMem) {
        if (gpr(a, 8)) {
            return ExecOp::kLea64;
        }
        if (gpr(a, 4)) {
            return ExecOp::kLea32;
        }
    }
    if (gpr(a, 8) && insn.opsize == 8) {
        switch (insn.mnemonic) {
        case Mnemonic::kInc:
            return ExecOp::kInc64;
        case Mnemonic::kDec:
            return ExecOp::kDec64;
        case Mnemonic::kAdd:
        case Mnemonic::kSub:
        case Mnemonic::kCmp:
        case Mnemonic::kTest:
        case Mnemonic::kAnd:
        case Mnemonic::kOr:
        case Mnemonic::kXor:
            if (b.kind == Operand::Kind::kImm || gpr(b, 8) ||
                (b.kind == Operand::Kind::kMem && b.size == 8)) {
                return ExecOp::kAlu64;
            }
            break;
        default:
            break;
        }
    }
    if (a.kind == Operand::Kind::kImm) {
        if (insn.mnemonic == Mnemonic::kJmp) {
            return ExecOp::kJmp;
        }
        if (insn.mnemonic == Mnemonic::kJcc) {
            if (insn.cc == Cc::kE) {
                return ExecOp::kJe;
            }
            return insn.cc == Cc::kNe ? ExecOp::kJne : ExecOp::kJcc;
        }
    }
    return ExecOp::kGeneric;
}

} // namespace

CachedInsn cache_insn(const DecodedInsn& insn) {
    return {.insn = insn, .op = classify(insn)};
}

ExitStatus Cpu::execute_cached(const CachedInsn& cached, uint64_t rip_after) {
    const DecodedInsn& insn = cached.insn;
    const Operand& a = insn.ops[0];
    const Operand& b = insn.ops[1];
    const auto dst = static_cast<unsigned>(a.reg);
    const auto src = static_cast<unsigned>(b.reg);
    uint64_t value = 0;
    switch (cached.op) {
    case ExecOp::kGeneric:
        return execute_insn(insn, rip_after);
    case ExecOp::kMov64:
        regs_[dst] = regs_[src];
        break;
    case ExecOp::kMov32:
        regs_[dst] = static_cast<uint32_t>(regs_[src]);
        break;
    case ExecOp::kImm64:
        regs_[dst] = static_cast<uint64_t>(b.disp);
        break;
    case ExecOp::kImm32:
        regs_[dst] = static_cast<uint32_t>(b.disp);
        break;
    case ExecOp::kLoad64:
    case ExecOp::kLoad32:
    case ExecOp::kLoad16:
    case ExecOp::kLoad8:
        if (!load(segment_address(insn, effective_address(b, rip_after)), b.size, value)) {
            return exit_;
        }
        regs_[dst] = a.size == 4 ? static_cast<uint32_t>(value) : value;
        break;
    case ExecOp::kStore64:
    case ExecOp::kStore32:
    case ExecOp::kStore16:
    case ExecOp::kStore8:
        if (!store(segment_address(insn, effective_address(a, rip_after)), a.size, regs_[src])) {
            return exit_;
        }
        break;
    case ExecOp::kStoreImm:
        if (!store(segment_address(insn, effective_address(a, rip_after)), a.size,
                   static_cast<uint64_t>(b.disp))) {
            return exit_;
        }
        break;
    case ExecOp::kLea64:
        regs_[dst] = effective_address(b, rip_after);
        break;
    case ExecOp::kLea32:
        regs_[dst] = static_cast<uint32_t>(effective_address(b, rip_after));
        break;
    case ExecOp::kAlu64: {
        const uint64_t lhs = regs_[dst];
        if (b.kind == Operand::Kind::kImm) {
            value = static_cast<uint64_t>(b.disp);
        } else if (b.kind == Operand::Kind::kMem) {
            if (!load(segment_address(insn, effective_address(b, rip_after)), 8, value)) {
                return exit_;
            }
        } else {
            value = regs_[src];
        }
        switch (insn.mnemonic) {
        case Mnemonic::kAdd:
            regs_[dst] = alu_add(lhs, value, 8, false);
            break;
        case Mnemonic::kSub:
            regs_[dst] = alu_sub(lhs, value, 8, false);
            break;
        case Mnemonic::kCmp:
            (void)alu_sub(lhs, value, 8, false);
            break;
        case Mnemonic::kTest:
            (void)alu_logic(lhs & value, 8);
            break;
        case Mnemonic::kAnd:
            regs_[dst] = alu_logic(lhs & value, 8);
            break;
        case Mnemonic::kOr:
            regs_[dst] = alu_logic(lhs | value, 8);
            break;
        default: // XOR
            regs_[dst] = alu_logic(lhs ^ value, 8);
            break;
        }
        break;
    }
    case ExecOp::kInc64:
    case ExecOp::kDec64: {
        const bool saved_cf = cf_;
        regs_[dst] = cached.op == ExecOp::kInc64 ? alu_add(regs_[dst], 1, 8, false)
                                                 : alu_sub(regs_[dst], 1, 8, false);
        cf_ = saved_cf;
        break;
    }
    case ExecOp::kJe:
        if (zf_) {
            rip_ = rip_after + static_cast<uint64_t>(a.disp);
        }
        break;
    case ExecOp::kJne:
        if (!zf_) {
            rip_ = rip_after + static_cast<uint64_t>(a.disp);
        }
        break;
    case ExecOp::kJcc:
        if (condition(insn.cc)) {
            rip_ = rip_after + static_cast<uint64_t>(a.disp);
        }
        break;
    case ExecOp::kJmp:
        rip_ = rip_after + static_cast<uint64_t>(a.disp);
        break;
    }
    retire(1);
    return exit_;
}

} // namespace micro_forge::arch::x86_64
