#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "arch/arm/cortex_m3/thumb32_fields.hpp"

#include <expected>

namespace micro_forge::cpu::arm::cortex_m3 {

using namespace thumb;

// ── Add/subtract (plain imm12): insn[25]=1 (hw1[9]) ──
// addw/subw (S=0) and adds.w/subs.w (S=1). imm12 = i:imm3:imm8 packed.
// PLAIN (not Thumb2ExpandImm). op = hw1[8:5]: 0000=ADD, 0101=SUB; S=hw1[4].
// Dispatched when (hw1 & 0xF800)==0xF000 && (hw1 & 0x0200)!=0 && (hw2 & 0x8000)==0.
CPU::CPUExpected<void> CortexM3CPU::t32_addsub_plain_imm(uint16_t hw1,
                                                    uint16_t hw2) {
    uint8_t op = (hw1 >> 5) & 0xF;
    bool s_bit = (hw1 >> 4) & 1;
    uint8_t rn = hw1 & 0xF;
    uint8_t rd = (hw2 >> 12) & 0xF;
    uint32_t imm12 = (((hw1 >> 10) & 0x1u) << 11) |
                     (((hw2 >> 12) & 0x7u) << 8) | (hw2 & 0xFFu);
    uint32_t a = rr(rn);
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
        update_flags(is_sub ? FlagPostOperation::Sub
                            : FlagPostOperation::Add,
                     a, imm12, result);
    }
    return wr(rd, result);
}

// ── Data processing (modified immediate): insn[25]=0 ──
// Dispatched when (hw1 & 0xF800)==0xF000 && (hw1 & 0x0200)==0 && (hw2 & 0x8000)==0.
CPU::CPUExpected<void> CortexM3CPU::t32_dataproc_imm(uint16_t hw1, uint16_t hw2) {
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
        if (op2 == 8 || op2 == 10 || op2 == 13 || op2 == 14 || op2 == 11) {
            uint32_t flag_rhs =
                op2 == 10   ? imm32 + ((xpsr_ & PSR_C) ? 1u : 0u)
                : op2 == 11 ? imm32 + ((xpsr_ & PSR_C) ? 0u : 1u)
                            : imm32;
            update_flags(op2 <= 10 ? FlagPostOperation::Add
                                   : FlagPostOperation::Sub,
                         rn_val, flag_rhs, result);
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
CPU::CPUExpected<void> CortexM3CPU::t32_dataproc_reg(uint16_t hw1, uint16_t hw2) {
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
        case 1:
            shifted = rm_val >> (shift_n == 0 ? 0 : shift_n);
            break;
        case 2: {
            if (shift_n == 0) {
                shifted = rm_val;
            } else {
                uint32_t sign = rm_val & 0x80000000u;
                shifted = rm_val >> shift_n;
                if (sign) {
                    shifted |= (0xFFFFFFFFu << (32 - shift_n));
                }
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
        case 3:
            result = ~shifted;
            break;
        case 4:
            result = rn_val ^ shifted;
            break;
        case 8:
            result = rn_val + shifted;
            break;
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
        if (op == 8 || op == 13 || op == 14) {
            update_flags(op <= 8 ? FlagPostOperation::Add
                                 : FlagPostOperation::Sub,
                         rn_val, shifted, result);
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
    uint32_t shift = rr(rm) & 0xFFu;
    uint32_t result = value;

    switch (shift_type) {
        case 0: // LSL
            result = shift == 0 ? value : (shift < 32 ? value << shift : 0);
            break;
        case 1: // LSR
            result = shift == 0 ? value : (shift < 32 ? value >> shift : 0);
            break;
        case 2: // ASR
            if (shift == 0) {
                result = value;
            } else if (shift >= 32) {
                result = (value & 0x80000000u) ? 0xFFFFFFFFu : 0;
            } else {
                result = static_cast<uint32_t>(
                    static_cast<int32_t>(value) >> shift);
            }
            break;
        case 3: { // ROR
            uint32_t rot = shift & 31u;
            result =
                rot == 0 ? value : ((value >> rot) | (value << (32 - rot)));
            break;
        }
    }

    auto w = wr(rd, result);
    if (!w) {
        return w;
    }
    if (s_bit) {
        update_nz(result);
    }
    return {};
}

} // namespace micro_forge::cpu::arm::cortex_m3
