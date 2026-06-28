#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "arch/arm/cortex_m3/thumb32_fields.hpp"

#include <expected>

namespace micro_forge::cpu::arm::cortex_m3 {

using namespace thumb;

// ── Add/subtract (plain imm12): insn[25]=1 (hw1[9]) ──
// addw/subw (S=0) and adds.w/subs.w (S=1). imm12 = i:imm3:imm8 packed.
// PLAIN (not Thumb2ExpandImm). op = hw1[8:5]: 0000=ADD, 0101=SUB; S=hw1[4].
// Dispatched when (hw1 & 0xF800)==0xF000 && (hw1 & 0x0200)!=0 && (hw2 &
// 0x8000)==0.
CPU::CPUExpected<void> CortexM3CPU::t32_addsub_plain_imm(uint16_t hw1,
                                                         uint16_t hw2) {
    uint8_t op = (hw1 >> 5) & 0xF;
    bool s_bit = (hw1 >> 4) & 1;
    uint8_t rn = hw1 & 0xF;
    uint8_t rd = (hw2 >> 12) & 0xF;
    uint32_t imm12 = (((hw1 >> 10) & 0x1u) << 11) |
                     (((hw2 >> 12) & 0x7u) << 8) | (hw2 & 0xFFu);
    uint32_t a;
    if (rn == 15) {
        // ADDW/SUBW with Rn=PC is ADR.W: base = Align(PC+4,4), not raw PC.
        auto pc_res = read_pc_raw();
        if (!pc_res) {
            return std::unexpected{pc_res.error()};
        }
        a = (*pc_res + 4) & ~0x3u;
    } else {
        a = rr(rn);
    }
    uint32_t result;
    bool is_sub;
    switch (op) {
        case 0x0:
            is_sub = false;
            result = a + imm12;
            break; // ADD.W/addw
        case 0x5:
            is_sub = true;
            result = a - imm12;
            break; // SUB.W/subw
        default:
            return std::unexpected{CPUError::IllegalInstruction};
    }
    if (s_bit) {
        update_flags(is_sub ? FlagPostOperation::Sub : FlagPostOperation::Add,
                     a, imm12, result);
    }
    return wr(rd, result);
}

// ── Data processing (modified immediate): insn[25]=0 ──
// Dispatched when (hw1 & 0xF800)==0xF000 && (hw1 & 0x0200)==0 && (hw2 &
// 0x8000)==0.
CPU::CPUExpected<void> CortexM3CPU::t32_dataproc_imm(uint16_t hw1,
                                                     uint16_t hw2) {
    uint8_t op2 = (hw1 >> 5) & 0xF;
    bool s_bit = (hw1 >> 4) & 1;
    uint8_t rn = thumb32::dp_rn(hw1);
    uint8_t rd = thumb32::dp_rd(hw2);
    uint32_t imm32 =
        thumb32::expand_imm12((hw1 >> 10) & 1, (hw2 >> 12) & 7, hw2 & 0xFF);
    uint32_t rn_val = rr(rn);
    uint32_t result;

    switch (op2) {
        case 0:
            result = rn_val & imm32;
            break; // AND
        case 1:
            result = rn_val & ~imm32;
            break; // BIC
        case 2:
            result = (rn == 15) ? imm32 : (rn_val | imm32);
            break; // ORR/MOV
        case 3:
            result = (rn == 15) ? ~imm32 : (rn_val | ~imm32);
            break; // ORN/MVN
        case 4:
            result = rn_val ^ imm32;
            break; // EOR
        case 8:
            result = rn_val + imm32;
            break; // ADD
        case 10:
            result = rn_val + imm32 + ((xpsr_ & PSR_C) ? 1u : 0u);
            break; // ADC
        case 11: {
            uint32_t borrow = (xpsr_ & PSR_C) ? 0u : 1u;
            result = rn_val - imm32 - borrow;
            break; // SBC
        }
        case 13:
            result = rn_val - imm32;
            break; // SUB
        case 14:
            result = imm32 - rn_val;
            break; // RSB
        default:
            return std::unexpected{CPUError::IllegalInstruction};
    }

    if (s_bit) {
        data_t cin = (xpsr_ & PSR_C) ? 1u : 0u;
        if (op2 == 8) { // ADD
            update_flags(FlagPostOperation::Add, rn_val, imm32, result);
        } else if (op2 == 10) { // ADC = rn + imm + C
            set_adc_flags(rn_val, imm32, cin, result);
        } else if (op2 == 11) { // SBC = rn - imm - !C
            set_sbc_flags(rn_val, imm32, cin, result);
        } else if (op2 == 13) { // SUB = rn - imm
            update_flags(FlagPostOperation::Sub, rn_val, imm32, result);
        } else if (op2 == 14) { // RSB = imm - rn; minuend is the immediate.
            update_flags(FlagPostOperation::Sub, imm32, rn_val, result);
        } else {
            update_nz(result);
        }
    }
    // CMP/CMN/TST/TEQ: S=1, Rd=15 → flags only, no register write
    if (s_bit && rd == 15) {
        return {};
    }
    return wr(rd, result);
}

// ── Data processing (shifted register): AND, ORR, EOR, ADD, SUB, etc. ──
// Dispatched when (hw1 & 0xFE00)==0xEA00 && (hw2 & 0x8000)==0.
CPU::CPUExpected<void> CortexM3CPU::t32_dataproc_reg(uint16_t hw1,
                                                     uint16_t hw2) {
    uint8_t op = (hw1 >> 5) & 0xF;
    bool s_bit = (hw1 >> 4) & 1;
    uint8_t rn = hw1 & 0xF;
    uint8_t rd = (hw2 >> 8) & 0xF;
    uint8_t rm = hw2 & 0xF;
    uint8_t imm3 = (hw2 >> 12) & 0x7;
    uint8_t imm2 = (hw2 >> 6) & 0x3;
    uint8_t shift_type = (hw2 >> 4) & 0x3;
    uint8_t shift_n = (imm3 << 2) | imm2;

    uint32_t rm_val = rr(rm);

    uint32_t shifted;
    switch (shift_type) {
        case 0:
            shifted = shift_n == 0 ? rm_val : rm_val << shift_n;
            break;
        case 1: // LSR; imm3:imm2==0 means shift-by-32 → result 0.
            shifted = shift_n == 0 ? 0u : (rm_val >> shift_n);
            break;
        case 2: { // ASR; imm3:imm2==0 means shift-by-32 → sign-extend.
            if (shift_n == 0) {
                shifted = (rm_val & 0x80000000u) ? 0xFFFFFFFFu : 0u;
            } else {
                uint32_t sign = rm_val & 0x80000000u;
                shifted = rm_val >> shift_n;
                if (sign) {
                    shifted |= (0xFFFFFFFFu << (32 - shift_n));
                }
            }
            break;
        }
        case 3: { // ROR; shift_n==0 means RRX (rotate-right-extend via C).
            if (shift_n == 0) {
                bool carry_in = (xpsr_ & PSR_C) != 0;
                shifted = (carry_in ? 0x80000000u : 0u) | (rm_val >> 1);
            } else {
                uint8_t n = shift_n & 0x1Fu;
                shifted = (rm_val >> n) | (rm_val << (32 - n));
            }
            break;
        }
        default:
            return std::unexpected{CPUError::IllegalInstruction};
    }

    uint32_t rn_val = rr(rn);
    uint32_t result;
    switch (op) {
        case 0:
            result = rn_val & shifted;
            break;
        case 1:
            result = rn_val & ~shifted;
            break;
        case 2:
            result = (rn == 15) ? shifted : (rn_val | shifted);
            break;
        case 3: // ORN = Rn | ~shifted; Rn=15 collapses to MVN (~shifted).
            result = (rn == 15) ? ~shifted : (rn_val | ~shifted);
            break;
        case 4:
            result = rn_val ^ shifted;
            break;
        case 8:
            result = rn_val + shifted;
            break;
        case 10: { // ADC = Rn + shifted + C
            result = rn_val + shifted + ((xpsr_ & PSR_C) ? 1u : 0u);
            break;
        }
        case 11: { // SBC = Rn - shifted - !C
            result = rn_val - shifted - ((xpsr_ & PSR_C) ? 0u : 1u);
            break;
        }
        case 13:
            result = rn_val - shifted;
            break;
        case 14:
            result = shifted - rn_val;
            break;
        default:
            return std::unexpected{CPUError::IllegalInstruction};
    }

    if (s_bit) {
        data_t cin = (xpsr_ & PSR_C) ? 1u : 0u;
        if (op == 8) { // ADD
            update_flags(FlagPostOperation::Add, rn_val, shifted, result);
        } else if (op == 10) { // ADC
            set_adc_flags(rn_val, shifted, cin, result);
        } else if (op == 11) { // SBC
            set_sbc_flags(rn_val, shifted, cin, result);
        } else if (op == 13) { // SUB = rn - shifted
            update_flags(FlagPostOperation::Sub, rn_val, shifted, result);
        } else if (op == 14) { // RSB = shifted - rn; minuend is the operand.
            update_flags(FlagPostOperation::Sub, shifted, rn_val, result);
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

// ── Shift register (LSL/LSR/ASR/ROR register) ──
// Dispatched when (hw1 & 0xFF00)==0xFA00 && (hw2 & 0xF0F0)==0xF000.
CPU::CPUExpected<void> CortexM3CPU::t32_shift_reg(uint16_t hw1, uint16_t hw2) {
    uint8_t rn = hw1 & 0xF;
    bool s_bit = (hw1 >> 4) & 1;
    uint8_t shift_type = (hw1 >> 5) & 0x3;
    uint8_t rd = (hw2 >> 8) & 0xF;
    uint8_t rm = hw2 & 0xF;

    uint32_t value = rr(rn);
    uint8_t amount = rr(rm) & 0xFFu;
    auto [result, carry] =
        barrel_shift(shift_type, value, amount, (xpsr_ & PSR_C) != 0);

    auto w = wr(rd, result);
    if (!w) {
        return w;
    }
    if (s_bit) {
        update_nz(result);
        if (carry) {
            xpsr_ |= PSR_C;
        } else {
            xpsr_ &= ~PSR_C;
        }
    }
    return {};
}

// ── CLZ / RBIT / REV.W / REV16.W / REVSH.W ──
// Dispatched when (hw1 & 0xFF00)==0xFA00 && (hw2 & 0xF000)==0xF000 &&
// (hw2 & 0x00F0)!=0 (op2 present; shift_reg handles op2==0).
CPU::CPUExpected<void> CortexM3CPU::t32_misc_reverse(uint16_t hw1,
                                                     uint16_t hw2) {
    uint8_t rd = (hw2 >> 8) & 0xFu;
    uint8_t rn = hw1 & 0xFu;
    uint8_t op2 = (hw2 >> 4) & 0xFu;
    uint32_t v = rr(rn);

    // op1 = hw1[7:4]. In the 0xFA00 reverse/CLZ space only op1==0xB (CLZ,
    // op2=8) and op1==0x9 (REV.W/REV16.W/RBIT/REVSH.W, op2 in {8,9,A,B}) are
    // assigned on Cortex-M3 — objdump-confirmed: rev/rev16/revsh/rbit encode
    // 0xFA9x, clz 0xFABx. ARMv7E-M DSP instructions (SXTAH/UXTAH/SXTB16/
    // UXTB16/SEL/QADD/QSUB/QDADD/QDSUB) share this 0xFA00 space with op1 in
    // {0,1,2,3,8,A}; they must fault, not mis-execute as a reverse op.
    uint8_t op1 = (hw1 >> 4) & 0xFu;
    if (op2 == 0x8u && op1 == 0xBu) {
        return wr(rd, std::countl_zero(v));
    }
    if (op1 != 0x9u) {
        return std::unexpected{CPUError::IllegalInstruction};
    }
    uint32_t result;
    switch (op2) {
        case 0x8u: // REV.W — byte-reverse (same result as 16-bit REV)
            result = ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
                     ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
            break;
        case 0x9u: // REV16.W — two halfword byte-swaps
            result = ((v & 0x00FF00FFu) << 8) | ((v & 0xFF00FF00u) >> 8);
            break;
        case 0xAu: { // RBIT — bit-reverse all 32 bits
            v = ((v & 0x55555555u) << 1) | ((v & 0xAAAAAAAAu) >> 1);
            v = ((v & 0x33333333u) << 2) | ((v & 0xCCCCCCCCu) >> 2);
            v = ((v & 0x0F0F0F0Fu) << 4) | ((v & 0xF0F0F0F0u) >> 4);
            v = ((v & 0x00FF00FFu) << 8) | ((v & 0xFF00FF00u) >> 8);
            result = (v << 16) | (v >> 16);
            break;
        }
        case 0xBu: { // REVSH.W — sign-extend low halfword byte-swap
            uint32_t r = ((v & 0x00FFu) << 8) | ((v & 0xFF00u) >> 8);
            result = static_cast<uint32_t>(
                static_cast<int32_t>(static_cast<int16_t>(r & 0xFFFFu)));
            break;
        }
        default:
            return std::unexpected{CPUError::IllegalInstruction};
    }
    return wr(rd, result);
}

// ── SSAT / USAT (saturate; writes APSR.Q on saturation) ──
// Dispatched when (hw1 & 0xFFD0)==0xF300 (SSAT) / 0xF380 (USAT).
CPU::CPUExpected<void> CortexM3CPU::t32_ssat_usat(uint16_t hw1, uint16_t hw2) {
    bool is_usat = (hw1 & 0x0080u) != 0u; // bit7: SSAT=0, USAT=1
    bool asr = (hw1 & 0x0020u) != 0u;     // bit5: shift type (1=asr, 0=lsl)
    uint8_t rn = hw1 & 0xFu;
    uint8_t rd = (hw2 >> 8) & 0xFu;
    uint8_t field = hw2 & 0x1Fu; // sat width field
    uint8_t imm3 = (hw2 >> 12) & 0x7u;
    uint8_t imm2 = (hw2 >> 6) & 0x3u;
    uint8_t shift = (imm3 << 2) | imm2;

    int32_t val = static_cast<int32_t>(rr(rn));
    val = asr ? (val >> shift)
              : static_cast<int32_t>(static_cast<uint32_t>(val) << shift);
    int64_t v = val;

    if (is_usat) {
        int64_t hi = (1ll << field) - 1; // USAT range [0, 2^field - 1]
        uint32_t result;
        if (v < 0) {
            result = 0u;
            xpsr_ |= PSR_Q;
        } else if (v > hi) {
            result = static_cast<uint32_t>(hi);
            xpsr_ |= PSR_Q;
        } else {
            result = static_cast<uint32_t>(v);
        }
        return wr(rd, result);
    }
    // SSAT range [-2^(field), 2^(field) - 1] (sat = field + 1).
    int64_t lo = -(1ll << field);
    int64_t hi = (1ll << field) - 1;
    uint32_t result;
    if (v < lo) {
        result = static_cast<uint32_t>(static_cast<uint64_t>(lo));
        xpsr_ |= PSR_Q;
    } else if (v > hi) {
        result = static_cast<uint32_t>(hi);
        xpsr_ |= PSR_Q;
    } else {
        result = static_cast<uint32_t>(v);
    }
    return wr(rd, result);
}

} // namespace micro_forge::cpu::arm::cortex_m3
