/**
 * @file dispatch.cpp
 * @brief Mnemonic dispatch: the batch-3 execution subset.
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/cpu.hpp"

#include <bit>

namespace micro_forge::arch::x86_64 {

ExitStatus Cpu::execute(const DecodedInsn& insn, uint64_t rip_after) {
    rip_ = rip_after; // control flow overwrites below
    const Operand& a = insn.ops[0];
    const Operand& b = insn.ops[1];
    const Operand& c = insn.ops[2];
    const unsigned osz = insn.opsize;
    uint64_t va = 0;
    uint64_t vb = 0;

    // SSE/SSE2 subset routes to its own executor (43 mnemonics); the
    // switch below owns everything else.
    if (is_sse_mnemonic(insn.mnemonic)) {
        return execute_sse(insn, rip_after);
    }

    switch (insn.mnemonic) {
    // --- data movement ---
    case Mnemonic::kMov:
        // Control/debug register moves: the operand bank is outside the
        // GPR storage, so they bypass read_operand/write_operand.
        if (a.kind == Operand::Kind::kReg && static_cast<unsigned>(a.reg) >= 32U) {
            if (!read_operand(insn, b, rip_after, vb)) {
                return exit_;
            }
            const auto bank = static_cast<unsigned>(a.reg);
            if (bank >= 32U && bank <= 35U) { // CR0/2/3/4
                cr_[bank - 32U] = vb;
                if (bank == 34U) { // CR3 write flushes the TLB
                    flush_tlb();
                }
            } else if (bank >= 36U && bank <= 41U) { // DR0-3/6/7
                dr_[bank - 36U] = vb;
            }
            break;
        }
        if (b.kind == Operand::Kind::kReg && static_cast<unsigned>(b.reg) >= 32U) {
            const auto bank = static_cast<unsigned>(b.reg);
            if (bank >= 32U && bank <= 35U) {
                vb = cr_[bank - 32U];
            } else if (bank >= 36U && bank <= 41U) {
                vb = dr_[bank - 36U];
            }
            if (!write_operand(insn, a, vb)) {
                return exit_;
            }
            break;
        }
        [[fallthrough]];
    case Mnemonic::kMovabs:
    case Mnemonic::kMovzx: // the source read is already zero-extended
        if (!read_operand(insn, b, rip_after, vb) || !write_operand(insn, a, vb)) {
            return exit_;
        }
        break;
    case Mnemonic::kMovsx:
    case Mnemonic::kMovsxd:
        if (!read_operand(insn, b, rip_after, vb)) {
            return exit_;
        }
        vb = sign_extend(vb, b.size);
        if (!write_operand(insn, a, vb)) {
            return exit_;
        }
        break;
    case Mnemonic::kXchg: {
        uint64_t va_old = 0;
        if (!read_operand(insn, a, rip_after, va_old) || !read_operand(insn, b, rip_after, vb)) {
            return exit_;
        }
        if (!write_operand(insn, a, vb) || !write_operand(insn, b, va_old)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kLea:
        // Address computation only; no memory access, no bank to refuse.
        if (!write_operand(insn, a, effective_address(b, rip_after))) {
            return exit_;
        }
        break;
    case Mnemonic::kPush: {
        if (!read_operand(insn, a, rip_after, va)) {
            return exit_;
        }
        // 64-bit push of an imm32 already arrived sign-extended from the
        // decoder (kSz4 under REX.W); 16-bit pushes mask in place.
        if (!push_value(va & width_mask(osz), osz)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kPop:
        if (!pop_value(osz, va)) {
            return exit_;
        }
        if (!write_operand(insn, a, va)) {
            return exit_;
        }
        break;
    case Mnemonic::kCmovcc:
        if (condition(insn.cc) &&
            (!read_operand(insn, b, rip_after, vb) || !write_operand(insn, a, vb))) {
            return exit_;
        }
        break;
    case Mnemonic::kSetcc:
        if (!write_operand(insn, a, condition(insn.cc) ? 1ULL : 0ULL)) {
            return exit_;
        }
        break;
    case Mnemonic::kCwde:
    case Mnemonic::kCdqe:
        // ax <- sext(al) / eax <- sext(ax) / rax <- sext(eax).
        write_gpr(Reg::kRax, osz, true, sign_extend(read_gpr(Reg::kRax, osz / 2U, true), osz / 2U));
        break;
    case Mnemonic::kCdq:
    case Mnemonic::kCqo:
        write_gpr(Reg::kRdx, osz, true,
                  (read_gpr(Reg::kRax, osz, true) & sign_bit(osz)) != 0 ? width_mask(osz) : 0);
        break;
    case Mnemonic::kBswap:
        if (!read_operand(insn, a, rip_after, va)) {
            return exit_;
        }
        // Mask to the operand width, byte-swap the whole 64-bit lane, then shift
        // the swapped bytes back down: byteswap alone lands the answer in the
        // high bits of the register-width word.
        va = std::byteswap(va & width_mask(osz)) >> (64U - (osz * 8U));
        if (!write_operand(insn, a, va)) {
            return exit_;
        }
        break;
    // --- integer ALU ---
    case Mnemonic::kAdd:
    case Mnemonic::kOr:
    case Mnemonic::kAdc:
    case Mnemonic::kSbb:
    case Mnemonic::kAnd:
    case Mnemonic::kSub:
    case Mnemonic::kXor:
    case Mnemonic::kCmp: {
        if (!read_operand(insn, a, rip_after, va) || !read_operand(insn, b, rip_after, vb)) {
            return exit_;
        }
        uint64_t result = 0;
        bool writes = true;
        switch (insn.mnemonic) {
        case Mnemonic::kAdd:
            result = alu_add(va, vb, osz, false);
            break;
        case Mnemonic::kAdc:
            result = alu_add(va, vb, osz, cf_);
            break;
        case Mnemonic::kSub:
        case Mnemonic::kCmp:
            result = alu_sub(va, vb, osz, false);
            writes = insn.mnemonic != Mnemonic::kCmp;
            break;
        case Mnemonic::kSbb:
            result = alu_sub(va, vb, osz, cf_);
            break;
        case Mnemonic::kAnd:
            result = alu_logic(va & vb, osz);
            break;
        case Mnemonic::kOr:
            result = alu_logic(va | vb, osz);
            break;
        default: // kXor
            result = alu_logic(va ^ vb, osz);
            break;
        }
        if (writes && !write_operand(insn, a, result)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kTest:
        if (!read_operand(insn, a, rip_after, va) || !read_operand(insn, b, rip_after, vb)) {
            return exit_;
        }
        alu_logic(va & vb, osz);
        break;
    case Mnemonic::kInc:
    case Mnemonic::kDec: {
        if (!read_operand(insn, a, rip_after, va)) {
            return exit_;
        }
        const bool saved_cf = cf_;
        const uint64_t result = (insn.mnemonic == Mnemonic::kInc) ? alu_add(va, 1, a.size, false)
                                                                  : alu_sub(va, 1, a.size, false);
        cf_ = saved_cf; // inc/dec leave CF alone
        if (!write_operand(insn, a, result)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kNeg:
        if (!read_operand(insn, a, rip_after, va)) {
            return exit_;
        }
        va = alu_sub(0, va, a.size, false);
        if (!write_operand(insn, a, va)) {
            return exit_;
        }
        break;
    case Mnemonic::kNot:
        if (!read_operand(insn, a, rip_after, va)) {
            return exit_;
        }
        if (!write_operand(insn, a, ~va & width_mask(a.size))) {
            return exit_;
        }
        break;
    case Mnemonic::kShl:
    case Mnemonic::kShr:
    case Mnemonic::kSar:
    case Mnemonic::kRol:
    case Mnemonic::kRor:
    case Mnemonic::kRcl:
    case Mnemonic::kRcr: {
        if (!read_operand(insn, a, rip_after, va) || !read_operand(insn, b, rip_after, vb)) {
            return exit_;
        }
        const uint64_t eff = vb & shift_count_mask(osz);
        if (eff != 0 &&
            (!alu_shift(insn.mnemonic, va, a.size, vb) || !write_operand(insn, a, va))) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kShld:
    case Mnemonic::kShrd: {
        uint64_t count = 0;
        if (!read_operand(insn, a, rip_after, va) || !read_operand(insn, b, rip_after, vb) ||
            !read_operand(insn, c, rip_after, count)) {
            return exit_;
        }
        const unsigned bits = 8U * osz;
        const uint64_t eff = count & shift_count_mask(osz);
        // Counts at or above the operand width leave the result
        // architecturally undefined; hardware shifts by width-1 and we
        // clamp the same way so `bits - eff` cannot underflow (the
        // batch-4 review found the 16-bit form reaching here with 17-31).
        const auto eff_c = static_cast<unsigned>(eff < bits ? eff : bits - 1U);
        if (eff_c != 0) {
            const uint64_t mask = width_mask(osz);
            uint64_t result = 0;
            if (insn.mnemonic == Mnemonic::kShld) {
                result = ((va << eff_c) | (vb >> (bits - eff_c))) & mask;
                cf_ = ((va >> (bits - eff_c)) & kernel::bit::mask_bit(0)) != 0;
                of_ = cf_ != ((result & sign_bit(osz)) != 0);
            } else {
                result = ((va >> eff_c) | (vb << (bits - eff_c))) & mask;
                cf_ = ((va >> (eff_c - 1U)) & kernel::bit::mask_bit(0)) != 0;
                of_ = (va & sign_bit(osz)) != 0;
            }
            set_zsp(result, osz);
            if (!write_operand(insn, a, result)) {
                return exit_;
            }
        }
        break;
    }
    case Mnemonic::kBt:
    case Mnemonic::kBts:
    case Mnemonic::kBtr:
    case Mnemonic::kBtc: {
        if (!read_operand(insn, b, rip_after, vb)) {
            return exit_; // bit index (imm or register)
        }
        if (a.kind == Operand::Kind::kReg) {
            if (!read_operand(insn, a, rip_after, va)) {
                return exit_;
            }
            const unsigned bits = 8U * a.size;
            const uint64_t pos = vb % bits;
            const uint64_t bit = (va >> pos) & kernel::bit::mask_bit(0);
            cf_ = bit != 0;
            uint64_t result = va;
            if (insn.mnemonic == Mnemonic::kBts) {
                result |= kernel::bit::mask_bit(pos);
            } else if (insn.mnemonic == Mnemonic::kBtr) {
                result &= ~kernel::bit::mask_bit(pos);
            } else if (insn.mnemonic == Mnemonic::kBtc) {
                result ^= kernel::bit::mask_bit(pos);
            }
            if (insn.mnemonic != Mnemonic::kBt && !write_operand(insn, a, result)) {
                return exit_;
            }
        } else {
            // Memory form: the index addresses a byte outside the loaded
            // cell and the bit is index & 7. Source width matters (the
            // batch-4 review): the imm8 form sign-extends from ONE byte,
            // the register form from the operand width.
            unsigned index_width = 8U;
            if (b.kind == Operand::Kind::kImm) {
                index_width = 1U;
            } else if (osz < 8U) {
                index_width = osz;
            }
            // The byte offset is the index arithmetic-shifted right by 3;
            // shifting the sign-extended word as UNSIGNED is NOT the same
            // (zero fill vs one fill), so the signed shift is spelled out.
            const auto index = static_cast<int64_t>(sign_extend(vb, index_width));
            const uint64_t addr =
                effective_address(a, rip_after) +
                static_cast<uint64_t>(index >> 3); // NOLINT(bugprone-signed-bitwise)
            const auto pos = static_cast<unsigned>(vb & 0x7ULL);
            uint64_t byte = 0;
            if (!load(addr, 1, byte)) {
                return exit_;
            }
            cf_ = ((byte >> pos) & kernel::bit::mask_bit(0)) != 0;
            if (insn.mnemonic != Mnemonic::kBt) {
                if (insn.mnemonic == Mnemonic::kBts) {
                    byte |= kernel::bit::mask_bit(pos);
                } else if (insn.mnemonic == Mnemonic::kBtr) {
                    byte &= ~kernel::bit::mask_bit(pos);
                } else { // kBtc
                    byte ^= kernel::bit::mask_bit(pos);
                }
                if (!store(addr, 1, byte)) {
                    return exit_;
                }
            }
        }
        break;
    }
    case Mnemonic::kBsf:
    case Mnemonic::kBsr: {
        if (!read_operand(insn, b, rip_after, vb)) {
            return exit_;
        }
        vb &= width_mask(osz);
        if (vb == 0) {
            zf_ = true; // destination is architecturally undefined
            break;
        }
        zf_ = false;
        const uint64_t pos = (insn.mnemonic == Mnemonic::kBsf)
                                 ? static_cast<uint64_t>(std::countr_zero(vb))
                                 : static_cast<uint64_t>(63U - std::countl_zero(vb));
        if (!write_operand(insn, a, pos)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kTzcnt:
    case Mnemonic::kLzcnt: {
        if (!read_operand(insn, b, rip_after, vb)) {
            return exit_;
        }
        vb &= width_mask(osz);
        const uint64_t bits = 8ULL * osz;
        uint64_t result = bits;
        if (vb != 0) {
            result = (insn.mnemonic == Mnemonic::kTzcnt)
                         ? static_cast<uint64_t>(std::countr_zero(vb))
                         : bits - static_cast<uint64_t>(std::bit_width(vb));
        }
        cf_ = vb == 0;
        of_ = false;
        zf_ = result == 0;
        sf_ = false;
        pf_ = parity_of(result);
        if (!write_operand(insn, a, result)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kXadd: {
        if (!read_operand(insn, a, rip_after, va) || !read_operand(insn, b, rip_after, vb)) {
            return exit_;
        }
        const uint64_t sum = alu_add(va, vb, a.size, false);
        if (!write_operand(insn, a, sum) || !write_operand(insn, b, va)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kCmpxchg: {
        if (!read_operand(insn, a, rip_after, va) || !read_operand(insn, b, rip_after, vb)) {
            return exit_;
        }
        const uint64_t acc = read_gpr(Reg::kRax, a.size, true);
        alu_sub(acc, va, a.size, false); // flags come from the compare
        if (zf_) {
            if (!write_operand(insn, a, vb)) {
                return exit_;
            }
        } else {
            write_gpr(Reg::kRax, a.size, true, va);
        }
        break;
    }
    case Mnemonic::kImul: {
        if (b.kind == Operand::Kind::kNone) {
            // One-operand form (F6/F7 /5): rDX:rAX = rAX * r/m.
            if (!read_operand(insn, a, rip_after, va)) {
                return exit_;
            }
            if (execute_grp3(insn, va).kind != ExitStatus::Kind::kRunning) {
                return exit_;
            }
            break;
        }
        if (execute_imul23(insn, rip_after).kind != ExitStatus::Kind::kRunning) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kMul:
    case Mnemonic::kDiv:
    case Mnemonic::kIdiv: // one-operand forms live in grp3
        if (!read_operand(insn, a, rip_after, va)) {
            return exit_;
        }
        if (execute_grp3(insn, va).kind != ExitStatus::Kind::kRunning) {
            return exit_;
        }
        break;
    // --- control flow ---
    case Mnemonic::kJmp:
        if (a.kind == Operand::Kind::kImm) {
            rip_ = rip_after + static_cast<uint64_t>(a.disp);
        } else {
            if (!read_operand(insn, a, rip_after, va)) {
                return exit_;
            }
            rip_ = va;
        }
        break;
    case Mnemonic::kJcc:
        if (condition(insn.cc)) {
            rip_ = rip_after + static_cast<uint64_t>(a.disp);
        }
        break;
    case Mnemonic::kCall:
        if (a.kind == Operand::Kind::kImm) {
            if (!push_value(rip_after, 8)) {
                return exit_;
            }
            rip_ = rip_after + static_cast<uint64_t>(a.disp);
        } else {
            if (!read_operand(insn, a, rip_after, va)) {
                return exit_;
            }
            if (!push_value(rip_after, 8)) {
                return exit_;
            }
            rip_ = va;
        }
        break;
    case Mnemonic::kRet:
        if (!pop_value(8, va)) {
            return exit_;
        }
        rip_ = va;
        if (a.kind == Operand::Kind::kImm) {
            regs_[index_of(Reg::kRsp)] += static_cast<uint64_t>(a.disp);
        }
        break;
    case Mnemonic::kLeave:
        regs_[index_of(Reg::kRsp)] = regs_[index_of(Reg::kRbp)];
        if (!pop_value(8, va)) {
            return exit_;
        }
        write_gpr(Reg::kRbp, 8, true, va);
        break;
    case Mnemonic::kLoop:
    case Mnemonic::kLoope:
    case Mnemonic::kLoopne: {
        const unsigned cx = index_of(Reg::kRcx);
        regs_[cx] = regs_[cx] - 1;
        const bool take = regs_[cx] != 0 && (insn.mnemonic == Mnemonic::kLoop ||
                                             (insn.mnemonic == Mnemonic::kLoope && zf_) ||
                                             (insn.mnemonic == Mnemonic::kLoopne && !zf_));
        if (take) {
            rip_ = rip_after + static_cast<uint64_t>(a.disp);
        }
        break;
    }
    case Mnemonic::kJrcxz:
        if (regs_[index_of(Reg::kRcx)] == 0) {
            rip_ = rip_after + static_cast<uint64_t>(a.disp);
        }
        break;
    // --- port I/O (batch 6b: see io.cpp) ---
    case Mnemonic::kIn:
    case Mnemonic::kOut:
        if (execute_ports(insn, rip_after).kind != ExitStatus::Kind::kRunning) {
            return exit_;
        }
        break;
    // --- system instructions (batch 6a: see sysregs.cpp) ---
    case Mnemonic::kPushfq:
    case Mnemonic::kPopfq:
    case Mnemonic::kCli:
    case Mnemonic::kSti:
    case Mnemonic::kClac:
    case Mnemonic::kStac:
    case Mnemonic::kFninit:
    case Mnemonic::kUd2:
    case Mnemonic::kCpuid:
    case Mnemonic::kRdtsc:
    case Mnemonic::kRdmsr:
    case Mnemonic::kWrmsr:
    case Mnemonic::kSwapgs:
    case Mnemonic::kLgdt:
    case Mnemonic::kLidt:
    case Mnemonic::kSgdt:
    case Mnemonic::kSidt:
    case Mnemonic::kRdrand:
    case Mnemonic::kRdsspq:
    case Mnemonic::kSldt:
    case Mnemonic::kStr:
    case Mnemonic::kLldt:
    case Mnemonic::kLtr:
    case Mnemonic::kInvlpg:
        if (execute_system(insn, rip_after).kind != ExitStatus::Kind::kRunning) {
            return exit_;
        }
        break;
    // --- no-ops and flag-only ---
    case Mnemonic::kIretq:
    case Mnemonic::kLretq:
    case Mnemonic::kSysret:
        if (execute_return(insn).kind != ExitStatus::Kind::kRunning) {
            return exit_;
        }
        break;
    case Mnemonic::kNop:
    case Mnemonic::kEndbr64:
    case Mnemonic::kPause:
    case Mnemonic::kLfence:
    case Mnemonic::kMfence:
    case Mnemonic::kSfence:
        break;
    case Mnemonic::kCld:
        df_ = false;
        break;
    case Mnemonic::kStd:
        df_ = true;
        break;
    case Mnemonic::kHlt:
        latch(ExitStatus{.kind = ExitStatus::Kind::kHalt, .code = 0, .rip = insn_rip_});
        retire(1);
        return exit_;
    default:
        return unsupported(insn);
    }
    retire(1);
    return exit_;
}
ExitStatus Cpu::execute_insn(const DecodedInsn& insn, uint64_t rip_after) {
    switch (insn.mnemonic) {
    case Mnemonic::kMovsb:
    case Mnemonic::kMovsw:
    case Mnemonic::kMovsd:
    case Mnemonic::kMovsq:
    case Mnemonic::kStosb:
    case Mnemonic::kStosw:
    case Mnemonic::kStosd:
    case Mnemonic::kStosq:
    case Mnemonic::kLodsb:
    case Mnemonic::kLodsw:
    case Mnemonic::kLodsd:
    case Mnemonic::kLodsq:
    case Mnemonic::kScasb:
    case Mnemonic::kScasw:
    case Mnemonic::kScasd:
    case Mnemonic::kScasq:
    case Mnemonic::kCmpsb:
    case Mnemonic::kCmpsw:
    case Mnemonic::kCmpsd:
    case Mnemonic::kCmpsq:
        return execute_string(insn, rip_after);
    case Mnemonic::kSyscall:
        return execute_syscall(rip_after);
    default:
        return execute(insn, rip_after);
    }
}

} // namespace micro_forge::arch::x86_64
