/**
 * @file execute.cpp
 * @brief Operand access, flag-recording ALU, and condition evaluation.
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/cpu.hpp"

#include "micro_forge/kernel/bit.hpp"

#include "internal.hpp"
#include <bit>

namespace micro_forge::arch::x86_64 {

uint64_t Cpu::read_gpr(Reg reg, unsigned width, bool rex) const {
    const unsigned id = index_of(reg);
    if (width >= 8) {
        return regs_[id];
    }
    if (width == 4) {
        return static_cast<uint32_t>(regs_[id]);
    }
    if (width == 2) {
        return static_cast<uint16_t>(regs_[id]);
    }
    if (!rex && id >= 4 && id <= 7) {
        return kernel::bit::extract(regs_[id - 4], {
                                                       .low = 8,
                                                       .width = 8,
                                                   }); // ah/ch/dh/bh
    }
    return regs_[id] & 0xFFULL;
}

void Cpu::write_gpr(Reg reg, unsigned width, bool rex, uint64_t value) {
    const unsigned id = index_of(reg);
    if (width >= 8) {
        regs_[id] = value;
    } else if (width == 4) {
        regs_[id] = static_cast<uint32_t>(value); // 32-bit writes zero-extend
    } else if (width == 2) {
        regs_[id] = kernel::bit::deposit(regs_[id],
                                         {
                                             .low = 0,
                                             .width = 16,
                                         },
                                         value);
    } else if (!rex && id >= 4 && id <= 7) {
        const unsigned host = id - 4; // ah/ch/dh/bh live in bits 15:8
        regs_[host] = kernel::bit::deposit(regs_[host],
                                           {
                                               .low = 8,
                                               .width = 8,
                                           },
                                           value);
    } else {
        regs_[id] = kernel::bit::deposit(regs_[id],
                                         {
                                             .low = 0,
                                             .width = 8,
                                         },
                                         value);
    }
}

uint64_t Cpu::effective_address(const Operand& op, uint64_t rip_after) const {
    if (op.rip_relative) {
        return rip_after + static_cast<uint64_t>(op.disp);
    }
    auto ea = static_cast<uint64_t>(op.disp);
    if (op.has_base) {
        ea += regs_[index_of(op.base)];
    }
    if (op.has_index) {
        ea += regs_[index_of(op.index)] * op.scale;
    }
    return ea;
}

bool Cpu::read_operand(const DecodedInsn& insn, const Operand& op, uint64_t rip_after,
                       uint64_t& out) {
    switch (op.kind) {
    case Operand::Kind::kNone:
        out = 0;
        return true;
    case Operand::Kind::kImm:
        out = static_cast<uint64_t>(op.disp);
        return true;
    case Operand::Kind::kReg:
    case Operand::Kind::kImplicit:
        if (static_cast<unsigned>(op.reg) >= 16U) {
            // XMM/CR/DR/segment banks have no storage in the batch-3
            // core; name the instruction instead of guessing a register.
            unsupported(insn);
            return false;
        }
        if (op.size == 0) {
            out = 0;
            return true;
        }
        out = read_gpr(op.reg, op.size, insn.rex_present);
        return true;
    case Operand::Kind::kMem:
        // FS/GS addressing adds the segment base (the per-CPU block the
        // syscall entry reaches through gs:0/gs:8).
        return load(segment_address(insn, effective_address(op, rip_after)), op.size, out);
    default:
        out = 0;
        return true;
    }
}

bool Cpu::write_operand(const DecodedInsn& insn, const Operand& op, uint64_t value) {
    if (op.kind == Operand::Kind::kMem) {
        // Writes resolve rip-relative against the next instruction too.
        const uint64_t ea = segment_address(insn, effective_address(op, insn_rip_ + cur_length_));
        return store(ea, op.size, value);
    }
    if (op.kind == Operand::Kind::kReg || op.kind == Operand::Kind::kImplicit) {
        if (static_cast<unsigned>(op.reg) >= 16U) {
            unsupported(insn);
            return false;
        }
        write_gpr(op.reg, op.size, insn.rex_present, value);
    }
    return true;
}

bool Cpu::parity_of(uint64_t value) {
    return (std::popcount(static_cast<uint8_t>(value)) % 2U) == 0U;
}

void Cpu::set_zsp(uint64_t result, unsigned width) {
    zf_ = (result & width_util::width_mask(width)) == 0;
    sf_ = (result & width_util::sign_bit(width)) != 0;
    pf_ = parity_of(result);
}

bool Cpu::condition(Cc cc) const {
    switch (cc) {
    case Cc::kO:
        return of_;
    case Cc::kNo:
        return !of_;
    case Cc::kB:
        return cf_;
    case Cc::kAe:
        return !cf_;
    case Cc::kE:
        return zf_;
    case Cc::kNe:
        return !zf_;
    case Cc::kBe:
        return cf_ || zf_;
    case Cc::kA:
        return !(cf_ || zf_);
    case Cc::kS:
        return sf_;
    case Cc::kNs:
        return !sf_;
    case Cc::kP:
        return pf_;
    case Cc::kNp:
        return !pf_;
    case Cc::kL:
        return sf_ != of_;
    case Cc::kGe:
        return sf_ == of_;
    case Cc::kLe:
        return zf_ || (sf_ != of_);
    case Cc::kG:
        return !zf_ && sf_ == of_;
    default:
        return false;
    }
}

uint64_t Cpu::alu_add(uint64_t a, uint64_t b, unsigned width, bool carry_in) {
    const uint64_t mask = width_util::width_mask(width);
    a &= mask;
    b &= mask;
    const uint64_t sum1 = a + b;
    const bool carry1 = sum1 < a;
    const uint64_t sum2 = sum1 + (carry_in ? 1ULL : 0ULL);
    const bool carry2 = sum2 < sum1;
    const uint64_t result = sum2 & mask;
    // Below width 8 the carry-out of the operand field is a shift; at
    // width 8 the addition itself wraps the uint64 domain.
    cf_ = (width < 8U) ? ((sum2 >> (8U * width)) != 0) : (carry1 || carry2);
    af_ = ((a ^ b ^ sum2) & 0x10ULL) != 0; // carry from bit 3 into bit 4
    of_ = ((~(a ^ b)) & (a ^ sum2) & width_util::sign_bit(width)) != 0;
    set_zsp(result, width);
    return result;
}

uint64_t Cpu::alu_sub(uint64_t a, uint64_t b, unsigned width, bool borrow_in) {
    const uint64_t mask = width_util::width_mask(width);
    a &= mask;
    b &= mask;
    const uint64_t diff1 = a - b;
    const bool borrow1 = a < b;
    const uint64_t diff2 = diff1 - (borrow_in ? 1ULL : 0ULL);
    const bool borrow2 = diff1 < (borrow_in ? 1ULL : 0ULL);
    const uint64_t result = diff2 & mask;
    cf_ = borrow1 || borrow2;
    af_ = (((a ^ b ^ diff2) >> 3U) & 1ULL) != 0;
    of_ = ((a ^ b) & (a ^ diff2) & width_util::sign_bit(width)) != 0;
    set_zsp(result, width);
    return result;
}

uint64_t Cpu::alu_logic(uint64_t result, unsigned width) {
    result &= width_util::width_mask(width);
    cf_ = false;
    of_ = false;
    af_ = false;
    set_zsp(result, width);
    return result;
}

bool Cpu::alu_shift(Mnemonic mn, uint64_t& value, unsigned width, uint64_t raw_count) {
    const unsigned bits = 8U * width;
    const uint64_t mask = width_util::width_mask(width);
    const uint64_t count = raw_count & ((width == 8U) ? 0x3FULL : 0x1FULL);
    value &= mask;
    if (count == 0) {
        return true; // no flags, no change
    }
    const uint64_t orig = value;
    switch (mn) {
    case Mnemonic::kShl: {
        value = (count <= bits) ? ((orig << count) & mask) : 0U;
        cf_ = (count <= bits) ? (((orig << (bits - count)) & 0x1ULL) != 0) : false;
        of_ = cf_ != ((value & width_util::sign_bit(width)) != 0);
        set_zsp(value, width);
        break;
    }
    case Mnemonic::kShr: {
        value = (count <= bits) ? (orig >> count) : 0U;
        cf_ = (count <= bits) ? (((orig >> (count - 1U)) & 0x1ULL) != 0) : false;
        of_ = (orig & width_util::sign_bit(width)) != 0;
        set_zsp(value, width);
        break;
    }
    case Mnemonic::kSar: {
        const auto signed_orig = static_cast<int64_t>(width_util::sign_extend(orig, width));
        const auto eff = static_cast<unsigned>(count > 63U ? 63U : count);
        value = static_cast<uint64_t>(signed_orig >> eff) & mask; // NOLINT(bugprone-signed-bitwise)
        cf_ = ((static_cast<uint64_t>(signed_orig) >> (eff - 1U)) & 0x1ULL) !=
              0; // NOLINT(bugprone-signed-bitwise)
        of_ = false;
        set_zsp(value, width);
        break;
    }
    case Mnemonic::kRol: {
        const auto eff = static_cast<unsigned>(count % bits);
        if (eff != 0) {
            value = ((orig << eff) | (orig >> (bits - eff))) & mask;
        }
        cf_ = (value & 0x1ULL) != 0;
        of_ = cf_ != ((value & width_util::sign_bit(width)) != 0);
        break;
    }
    case Mnemonic::kRor: {
        const auto eff = static_cast<unsigned>(count % bits);
        if (eff != 0) {
            value = ((orig >> eff) | (orig << (bits - eff))) & mask;
        }
        cf_ = (value & width_util::sign_bit(width)) != 0;
        of_ = cf_ != ((value & (width_util::sign_bit(width) >> 1U)) != 0);
        break;
    }
    case Mnemonic::kRcl:
    case Mnemonic::kRcr: {
        const auto eff = static_cast<unsigned>(count % (bits + 1U));
        value = orig;
        for (unsigned i = 0; i < eff; ++i) {
            const bool old_cf = cf_;
            if (mn == Mnemonic::kRcl) {
                cf_ = (value & width_util::sign_bit(width)) != 0;
                value = ((value << 1U) & mask) | (old_cf ? 1ULL : 0ULL);
            } else {
                cf_ = (value & 0x1ULL) != 0;
                value = (value >> 1U) | (old_cf ? width_util::sign_bit(width) : 0ULL);
            }
        }
        if (eff != 0) {
            if (mn == Mnemonic::kRcl) {
                of_ = cf_ != ((value & width_util::sign_bit(width)) != 0);
            } else {
                of_ = cf_ != ((value & (width_util::sign_bit(width) >> 1U)) != 0);
            }
        }
        break;
    }
    default:
        return false;
    }
    return true;
}

} // namespace micro_forge::arch::x86_64
