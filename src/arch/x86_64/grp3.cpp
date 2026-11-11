/**
 * @file grp3.cpp
 * @brief Multiply and divide families: one-operand grp3 forms plus the
 *        two/three-operand imul encodings.
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/cpu.hpp"

#include "micro_forge/arch/x86_64/mul_wide.hpp"
#include "micro_forge/kernel/bit.hpp"

#include "internal.hpp"
#include <limits>

namespace micro_forge::arch::x86_64 {

namespace {

/// 128/64 -> 64 unsigned division by shift-subtract, no __int128.
///
/// The carry out of r << 1 is tracked explicitly, so divisors above
/// 2^63 stay correct. The caller guarantees divisor != 0 and
/// high < divisor (the quotient then fits 64 bits).
uint64_t udiv128(uint64_t high, uint64_t low, uint64_t divisor, uint64_t& rem) {
    uint64_t q = 0;
    uint64_t r = high;
    for (int i = 63; i >= 0; --i) {
        const uint64_t carry = r >> 63U;
        r = (r << 1U) | ((low >> static_cast<unsigned>(i)) & 1ULL);
        q <<= 1U;
        if (carry != 0U || r >= divisor) {
            r -= divisor;
            q |= 1ULL;
        }
    }
    rem = r;
    return q;
}

/// In-place magnitude of a 128-bit two's-complement value.
///
/// @return true when the value was negative (and got negated).
bool abs128(uint64_t& hi, uint64_t& lo) {
    if ((hi >> 63U) == 0U) {
        return false;
    }
    lo = ~lo + 1U;
    hi = ~hi + (lo == 0U ? 1U : 0U); // the carry from the low negate
    return true;
}

/// Which half of the product escapes the low operand-width: AH at width 1,
/// the plain high word at width 8, else the shifted-out high half.
uint64_t mul_upper(uint64_t low, uint64_t high, unsigned width) {
    if (width == 1U) {
        return kernel::bit::extract(low, {
                                             .low = 8,
                                             .width = 8,
                                         });
    }
    if (width == 8U) {
        return high;
    }
    if (width == 4U) {
        return low >> 32U;
    }
    return low >> 16U; // width == 2
}

} // namespace

ExitStatus Cpu::execute_grp3(const DecodedInsn& insn, uint64_t src) {
    const unsigned width = insn.opsize;
    const uint64_t mask = width_util::width_mask(width);
    const auto fault_divide = [this] {
        latch(ExitStatus{
            .kind = ExitStatus::Kind::kFault,
            .code = insn_rip_,
            .rip = insn_rip_,
            .detail = static_cast<uint64_t>(FaultId::kDivide),
        });
    };

    switch (insn.mnemonic) {
    case Mnemonic::kMul: {
        const uint64_t acc = read_gpr(Reg::kRax, width, true);
        uint64_t high = 0;
        uint64_t low = 0;
        if (width == 8U) {
            low = mul_wide(acc, src, high);
        } else {
            low = acc * src; // at most 32x32 -> 64 bits, never overflows
        }
        const uint64_t upper = mul_upper(low, high, width);
        if (width == 1U) {
            // AX = AL * src: low byte AL, high byte AH.
            write_gpr(Reg::kRax, 2, true,
                      kernel::bit::deposit(low,
                                           {
                                               .low = 8,
                                               .width = 8,
                                           },
                                           upper));
        } else {
            write_gpr(Reg::kRax, width, true, low & mask);
            write_gpr(Reg::kRdx, width, true, upper & mask);
        }
        cf_ = of_ = upper != 0;
        break;
    }
    case Mnemonic::kImul: {
        const auto acc =
            static_cast<int64_t>(width_util::sign_extend(read_gpr(Reg::kRax, width, true), width));
        const auto val = static_cast<int64_t>(width_util::sign_extend(src, width));
        const int64_t high = imul_high(acc, val);
        const uint64_t low = static_cast<uint64_t>(acc) * static_cast<uint64_t>(val);
        const uint64_t low_masked = low & mask;
        bool fits = false;
        if (width == 8U) {
            fits = static_cast<uint64_t>(high) == static_cast<uint64_t>(sign_fill_of(low));
        } else {
            fits = static_cast<int64_t>(width_util::sign_extend(low_masked, width)) == acc * val;
        }
        const uint64_t upper = mul_upper(low, static_cast<uint64_t>(high), width);
        if (width == 1U) {
            write_gpr(Reg::kRax, 2, true,
                      kernel::bit::deposit(low,
                                           {
                                               .low = 8,
                                               .width = 8,
                                           },
                                           upper));
        } else {
            write_gpr(Reg::kRax, width, true, low_masked);
            write_gpr(Reg::kRdx, width, true, upper & mask);
        }
        cf_ = of_ = !fits;
        break;
    }
    case Mnemonic::kDiv: {
        if (src == 0) {
            fault_divide();
            return exit_;
        }
        if (width == 8U) {
            // The dividend is the rdx:rax pair, a true 128-bit value:
            // divide via shift-subtract. hi >= src means the quotient
            // does not fit 64 bits, which is #DE.
            const uint64_t hi = read_gpr(Reg::kRdx, 8, true);
            const uint64_t lo = read_gpr(Reg::kRax, 8, true);
            if (hi >= src) {
                fault_divide();
                return exit_;
            }
            uint64_t rem = 0;
            const uint64_t quot = udiv128(hi, lo, src, rem);
            write_gpr(Reg::kRax, 8, true, quot);
            write_gpr(Reg::kRdx, 8, true, rem);
            break;
        }
        uint64_t dividend = 0;
        if (width == 1U) {
            dividend = read_gpr(Reg::kRax, 2, true);
        } else if (width == 4U) {
            dividend = (read_gpr(Reg::kRdx, 4, true) << 32U) | read_gpr(Reg::kRax, 4, true);
        } else { // width == 2
            dividend = (read_gpr(Reg::kRdx, 2, true) << 16U) | read_gpr(Reg::kRax, 2, true);
        }
        const uint64_t quot = dividend / src;
        const uint64_t rem = dividend % src;
        if (width == 1U) {
            if (quot > 0xFFULL) {
                fault_divide();
                return exit_;
            }
            write_gpr(Reg::kRax, 2, true,
                      kernel::bit::deposit(quot,
                                           {
                                               .low = 8,
                                               .width = 8,
                                           },
                                           rem));
            break;
        }
        if (quot > mask) {
            fault_divide();
            return exit_;
        }
        write_gpr(Reg::kRax, width, true, quot);
        write_gpr(Reg::kRdx, width, true, rem);
        break;
    }
    case Mnemonic::kIdiv: {
        const auto divisor = static_cast<int64_t>(width_util::sign_extend(src, width));
        if (divisor == 0) {
            fault_divide();
            return exit_;
        }
        if (width == 8U) {
            // s128 dividend / s64 divisor, on magnitudes: signs applied
            // after the unsigned division, quotient range-checked against
            // the signed envelope (only -2^63 may arrive as 2^63).
            uint64_t hi = read_gpr(Reg::kRdx, 8, true);
            uint64_t lo = read_gpr(Reg::kRax, 8, true);
            const bool neg_dividend = abs128(hi, lo);
            auto mag_divisor = static_cast<uint64_t>(divisor);
            bool neg_divisor = false;
            if (divisor < 0) {
                neg_divisor = true;
                mag_divisor = ~mag_divisor + 1U; // INT64_MIN negates to itself
            }
            if (hi >= mag_divisor) {
                fault_divide(); // |quotient| > 2^64 - 1
                return exit_;
            }
            uint64_t mag_rem = 0;
            const uint64_t mag_q = udiv128(hi, lo, mag_divisor, mag_rem);
            const uint64_t k_min_mag = 1ULL << 63U;
            const bool same_sign = neg_dividend == neg_divisor;
            if (mag_q > k_min_mag || (mag_q == k_min_mag && same_sign)) {
                fault_divide(); // positive overflow has no +2^63
                return exit_;
            }
            const uint64_t quot = same_sign ? mag_q : ~mag_q + 1U;
            const uint64_t rem = neg_dividend ? ~mag_rem + 1U : mag_rem;
            write_gpr(Reg::kRax, 8, true, quot);
            write_gpr(Reg::kRdx, 8, true, rem);
            break;
        }
        int64_t dividend = 0;
        if (width == 1U) {
            dividend = static_cast<int16_t>(read_gpr(Reg::kRax, 2, true));
        } else {
            const uint64_t raw =
                (width == 4U)
                    ? (read_gpr(Reg::kRdx, 4, true) << 32U) | read_gpr(Reg::kRax, 4, true)
                    : (read_gpr(Reg::kRdx, 2, true) << 16U) | read_gpr(Reg::kRax, 2, true);
            dividend = static_cast<int64_t>(width_util::sign_extend(raw, 2U * width));
        }
        if (dividend == std::numeric_limits<int64_t>::min() && divisor == -1) {
            fault_divide();
            return exit_;
        }
        const int64_t quot = dividend / divisor;
        const int64_t rem = dividend % divisor;
        const int64_t q_max = (width >= 8U)
                                  ? std::numeric_limits<int64_t>::max()
                                  : static_cast<int64_t>((1ULL << ((8U * width) - 1U)) - 1ULL);
        const int64_t q_min = (width >= 8U) ? std::numeric_limits<int64_t>::min() : -q_max - 1;
        if (quot > q_max || quot < q_min) {
            fault_divide();
            return exit_;
        }
        if (width == 1U) {
            write_gpr(Reg::kRax, 2, true,
                      kernel::bit::deposit(static_cast<uint64_t>(quot),
                                           {
                                               .low = 8,
                                               .width = 8,
                                           },
                                           static_cast<uint64_t>(rem)));
        } else {
            write_gpr(Reg::kRax, width, true, static_cast<uint64_t>(quot));
            write_gpr(Reg::kRdx, width, true, static_cast<uint64_t>(rem));
        }
        break;
    }
    default:
        return unsupported(insn);
    }
    return exit_;
}

ExitStatus Cpu::execute_imul23(const DecodedInsn& insn, uint64_t rip_after) {
    const Operand& a = insn.ops[0];
    const Operand& b = insn.ops[1];
    const Operand& c = insn.ops[2];
    const unsigned osz = insn.opsize;
    uint64_t vb = 0;
    if (!read_operand(insn, b, rip_after, vb)) {
        return exit_;
    }
    uint64_t imm = 0;
    if (c.kind != Operand::Kind::kNone && !read_operand(insn, c, rip_after, imm)) {
        return exit_;
    }
    const auto lhs = static_cast<int64_t>(width_util::sign_extend(
        c.kind != Operand::Kind::kNone ? vb : read_gpr(a.reg, a.size, true), osz));
    const auto rhs = static_cast<int64_t>(
        width_util::sign_extend(c.kind != Operand::Kind::kNone ? imm : vb, osz));
    const uint64_t low = static_cast<uint64_t>(lhs) * static_cast<uint64_t>(rhs);
    const uint64_t low_masked = low & width_util::width_mask(osz);
    bool fits = false;
    if (osz == 8U) {
        fits = imul_high(lhs, rhs) == sign_fill_of(low);
    } else {
        fits = static_cast<int64_t>(width_util::sign_extend(low_masked, osz)) == lhs * rhs;
    }
    cf_ = of_ = !fits;
    if (!write_operand(insn, a, low_masked)) {
        return exit_;
    }
    return exit_;
}

} // namespace micro_forge::arch::x86_64
