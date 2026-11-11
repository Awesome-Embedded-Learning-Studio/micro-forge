/**
 * @file sse_ext.cpp
 * @brief New-kernel SSE face (2026-11-10 profile): psadbw, pextrw, movss.
 * @version 0.1.0
 *
 * Split out of sse.cpp at its 600-line ceiling. Reach here through the
 * execute_sse() default arm; operand plumbing mirrors the parent file.
 */

#include "micro_forge/arch/x86_64/cpu.hpp"

#include "arch/x86_64/sse_ops.hpp"

namespace micro_forge::arch::x86_64 {

using sse_ops::Vec128;

ExitStatus Cpu::execute_sse_ext(const DecodedInsn& insn, uint64_t rip_after) {
    const Operand& a = insn.ops[0];
    const Operand& b = insn.ops[1];

    const auto read_vec = [&](const Operand& op, Vec128& out) -> bool {
        if (op.kind == Operand::Kind::kReg) {
            out = xmm(op.reg);
            return true;
        }
        if (op.kind == Operand::Kind::kMem) {
            const uint64_t ea = effective_address(op, rip_after);
            uint64_t low = 0;
            uint64_t high = 0;
            if (!load(ea, 8, low) || !load(ea + 8U, 8, high)) {
                return false;
            }
            out = Vec128{low, high};
            return true;
        }
        return false;
    };
    const auto write_vec = [&](const Operand& op, const Vec128& value) -> bool {
        if (op.kind == Operand::Kind::kReg) {
            set_xmm(op.reg, value);
            return true;
        }
        if (op.kind == Operand::Kind::kMem) {
            const uint64_t ea = effective_address(op, rip_after);
            return store(ea, 8, value[0]) && store(ea + 8U, 8, value[1]);
        }
        return false;
    };

    switch (insn.mnemonic) {
    case Mnemonic::kPshuflw:
    case Mnemonic::kPshufhw: {
        Vec128 src{};
        if (!read_vec(insn.ops[1], src)) {
            return exit_;
        }
        uint64_t ctrl = 0;
        if (insn.ops[2].kind == Operand::Kind::kImm) {
            ctrl = static_cast<uint64_t>(insn.ops[2].disp);
        }
        Vec128 out{};
        if (insn.mnemonic == Mnemonic::kPshuflw) {
            sse_ops::pshuflw(out, src, ctrl);
        } else {
            sse_ops::pshufhw(out, src, ctrl);
        }
        if (!write_vec(insn.ops[0], out)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kPsadbw: {
        Vec128 dst{};
        Vec128 src{};
        if (!read_vec(a, dst) || !read_vec(b, src)) {
            return exit_;
        }
        sse_ops::psadbw(dst, src);
        if (!write_vec(a, dst)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kPextrw: {
        // r32 = word (imm & 3) of xmm; reg-form only, like pmovmskb.
        if (b.kind != Operand::Kind::kReg) {
            return unsupported(insn);
        }
        uint64_t ctrl = 0;
        if (insn.ops[2].kind == Operand::Kind::kImm) {
            ctrl = static_cast<uint64_t>(insn.ops[2].disp);
        }
        const Vec128 src = xmm(b.reg);
        const auto word = static_cast<unsigned>(ctrl & 3U);
        const auto value = static_cast<uint64_t>((src[word / 2U] >> (16U * (word % 2U))) & 0xFFFFULL);
        if (!write_operand(insn, a, value)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kMovss: {
        // Bit mover, no float arithmetic: low 32 bits travel, the upper
        // 96 bits of an XMM destination survive.
        if (a.kind == Operand::Kind::kMem) { // F3 0F 11: store 32 bits
            uint64_t v = 0;
            if (b.kind != Operand::Kind::kReg) {
                return unsupported(insn);
            }
            v = xmm(b.reg)[0] & 0xFFFF'FFFFULL;
            const uint64_t ea = effective_address(a, rip_after);
            if (!store(ea, 4, v)) {
                return exit_;
            }
            break;
        }
        Vec128 dst = xmm(a.reg);
        uint64_t src32 = 0;
        if (b.kind == Operand::Kind::kMem) {
            if (!load(effective_address(b, rip_after), 4, src32)) {
                return exit_;
            }
        } else if (b.kind == Operand::Kind::kReg) {
            src32 = xmm(b.reg)[0] & 0xFFFF'FFFFULL;
        } else {
            return unsupported(insn);
        }
        dst[0] = (dst[0] & ~0xFFFF'FFFFULL) | src32;
        set_xmm(a.reg, dst);
        break;
    }
    default:
        return unsupported(insn);
    }
    return exit_; // kRunning: the caller retires the instruction
}

} // namespace micro_forge::arch::x86_64
