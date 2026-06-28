#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "arch/arm/cortex_m3/thumb_fields.hpp"

#include <expected>
#include <optional>

namespace micro_forge::cpu::arm::cortex_m3 {

using namespace thumb;

// ── 16-bit Thumb data-processing family handlers ──
// Split out of execute_16bit (cortex_m3_thumb16.cpp). Each returns the result
// of its prefix-/decode_key-matched block; execute_16bit dispatches. Bodies are
// moved verbatim from the original switch arms — rr/wr are the shared member
// accessors. An arm that ended in `break` (falling through to the function's
// trailing `return {}`) now ends in `return {}` directly; a switch with no
// default gains an unreachable trailing `return IllegalInstruction` (the old
// arms fell through to the next prefix probe, which a standalone handler can't).

// Sign/zero extend byte/halfword (0xB200): SXTH/SXTB/UXTH/UXTB.
CPU::CPUExpected<void> CortexM3CPU::t16_extend(uint16_t insn) {
    uint8_t rm = (insn >> 3) & 0x7u;
    uint8_t rd = insn & 0x7u;
    uint8_t op = (insn >> 6) & 0x3u;
    data_t value = rr(rm);
    switch (op) {
        case 0b00:
            return wr(rd, static_cast<data_t>(static_cast<int32_t>(
                              static_cast<int16_t>(value & 0xFFFFu))));
        case 0b01:
            return wr(rd, static_cast<data_t>(static_cast<int32_t>(
                              static_cast<int8_t>(value & 0xFFu))));
        case 0b10:
            return wr(rd, value & 0xFFFFu);
        case 0b11:
            return wr(rd, value & 0xFFu);
    }
    return std::unexpected{CPUError::IllegalInstruction}; // 2-bit op covers 0-3
}

// Byte-reversal (0xBA00): REV/REV16/REVSH.
CPU::CPUExpected<void> CortexM3CPU::t16_reverse(uint16_t insn) {
    uint8_t rm = (insn >> 3) & 0x7u;
    uint8_t rd = insn & 0x7u;
    uint8_t op = (insn >> 6) & 0x3u;
    data_t value = rr(rm);
    switch (op) {
        case 0b00:
            return wr(rd, ((value & 0x000000FFu) << 24) |
                              ((value & 0x0000FF00u) << 8) |
                              ((value & 0x00FF0000u) >> 8) |
                              ((value & 0xFF000000u) >> 24));
        case 0b01:
            return wr(rd, ((value & 0x00FF00FFu) << 8) |
                              ((value & 0xFF00FF00u) >> 8));
        case 0b11: {
            uint32_t rev_half =
                ((value & 0x00FFu) << 8) | ((value & 0xFF00u) >> 8);
            return wr(rd, static_cast<data_t>(static_cast<int32_t>(
                              static_cast<int16_t>(rev_half))));
        }
        default:
            return std::unexpected{CPUError::IllegalInstruction};
    }
}

// Shift immediate (0b00000-0b00010): LSL/LSR/ASR #imm.
CPU::CPUExpected<void> CortexM3CPU::t16_shift_imm(uint16_t insn) {
    uint8_t op = (insn >> 11) & 0x3; // 0=LSL, 1=LSR, 2=ASR
    uint8_t imm = imm5(insn);
    uint8_t rm = rn3(insn);
    uint8_t rd = rd3(insn);
    data_t val = rr(rm);
    // LSR/ASR encoded shift of 0 means shift-by-32; LSL 0 = no shift.
    uint8_t amount = (op == 0b00) ? imm : (imm == 0 ? 32 : imm);
    auto [result, carry] =
        barrel_shift(op, val, amount, (xpsr_ & PSR_C) != 0);
    auto res = wr(rd, result);
    if (!res) {
        return res;
    }
    update_nz(result);
    // Shift instructions update C from the shifter carry-out (LSL #0
    // returns carry_in, so C is unchanged in that case).
    if (carry) {
        xpsr_ |= PSR_C;
    } else {
        xpsr_ &= ~PSR_C;
    }
    return {};
}

// Add/subtract register or 3-bit immediate (0b00011).
CPU::CPUExpected<void> CortexM3CPU::t16_addsub_reg3(uint16_t insn) {
    bool is_imm = (insn >> 10) & 0x1;
    bool is_sub = (insn >> 9) & 0x1;
    uint8_t rm_or_imm = rm3(insn);
    uint8_t rn = rn3(insn);
    uint8_t rd = rd3(insn);
    data_t a = rr(rn);
    data_t b = is_imm ? rm_or_imm : rr(rm_or_imm);
    data_t result = is_sub ? a - b : a + b;

    if (is_sub) {
        update_flags(FlagPostOperation::Sub, a, b, result);
    } else {
        update_flags(FlagPostOperation::Add, a, b, result);
    }
    return wr(rd, result);
}

// MOVS/CMP/ADDS/SUBS Rd, imm8 (0b00100-0b00111).
CPU::CPUExpected<void> CortexM3CPU::t16_imm8_dataops(uint16_t insn) {
    switch (decode_key(insn)) {
        // MOVS Rd, imm8
        case 0b00100: {
            uint8_t rd = rd8(insn);
            data_t val = imm8(insn);
            auto res = wr(rd, val);
            if (!res) {
                return res;
            }
            update_nz(val);
            return {};
        }
        // CMP Rn, imm8
        case 0b00101: {
            data_t a = rr(rd8(insn));
            data_t b = imm8(insn);
            update_flags(FlagPostOperation::Sub, a, b, a - b);
            return {};
        }
        // ADDS Rd, imm8
        case 0b00110: {
            uint8_t rd = rd8(insn);
            data_t a = rr(rd), b = imm8(insn);
            data_t result = a + b;
            update_flags(FlagPostOperation::Add, a, b, result);
            return wr(rd, result);
        }
        // SUBS Rd, imm8
        case 0b00111: {
            uint8_t rd = rd8(insn);
            data_t a = rr(rd), b = imm8(insn);
            data_t result = a - b;
            update_flags(FlagPostOperation::Sub, a, b, result);
            return wr(rd, result);
        }
    }
    return std::unexpected{CPUError::IllegalInstruction};
}

// Special data instructions / BX (0b01000, bit10=1): ADD/CMP/MOV high, BX/BLX.
CPU::CPUExpected<void> CortexM3CPU::t16_special_bx(uint16_t insn) {
    uint8_t op = (insn >> 8) & 0x3;
    uint8_t rm = rm4(insn);
    uint8_t rd = rd4(insn);

    switch (op) {
        case 0b00:
            return wr(rd, rr(rd) + rr(rm)); // ADD high
        case 0b01: {                        // CMP high
            data_t a = rr(rd), b = rr(rm);
            update_flags(FlagPostOperation::Sub, a, b, a - b);
            return {};
        }
        case 0b10:
            return wr(rd, rr(rm)); // MOV high
        case 0b11: { // BX / BLX register — bit[7]: 0=BX, 1=BLX
            bool is_blx = (insn >> 7) & 1;
            data_t target = rr(rm);
            if (is_blx) {
                // LR = address of next instruction | Thumb bit.
                auto pc_res = read_pc_raw();
                if (!pc_res) {
                    return std::unexpected{pc_res.error()};
                }
                auto lr_wr = wr(14, (*pc_res + 2u) | 1u);
                if (!lr_wr) {
                    return lr_wr;
                }
            }
            return write_pc(target);
        }
    }
    return std::unexpected{CPUError::IllegalInstruction}; // 2-bit op covers 0-3
}

// Data processing register (0b01000, bit10=0): AND/EOR/shift-reg/ADC/SBC/
// ROR/TST/RSB/CMN/ORR/MUL/BIC/MVN.
CPU::CPUExpected<void> CortexM3CPU::t16_dataproc_reg(uint16_t insn) {
    // op=bits[9:6], Rm/Rs=bits[5:3], Rdn/Rd=bits[2:0].
    // (rm3() reads bits[8:6], which is the store-reg-offset Rm field,
    //  NOT the data-proc Rm — that was the bug.)
    uint8_t op = (insn >> 6) & 0xF;
    uint8_t rm = (insn >> 3) & 0x7u;
    uint8_t rd = rd3(insn);
    data_t a = rr(rd), b = rr(rm);
    data_t result;
    // Set only by the shift-by-register ops (LSL/LSR/ASR/ROR): the
    // shifter carry-out drives C. nullopt → C unchanged.
    std::optional<bool> shift_carry;

    switch (op) {
        case 0x0:
            result = a & b;
            break;
        case 0x1:
            result = a ^ b;
            break;
        case 0x2: { // LSL register
            auto s = barrel_shift(0, a, b & 0xFF, (xpsr_ & PSR_C) != 0);
            result = s.value;
            shift_carry = s.carry;
            break;
        }
        case 0x3: { // LSR register
            auto s = barrel_shift(1, a, b & 0xFF, (xpsr_ & PSR_C) != 0);
            result = s.value;
            shift_carry = s.carry;
            break;
        }
        case 0x4: { // ASR register
            auto s = barrel_shift(2, a, b & 0xFF, (xpsr_ & PSR_C) != 0);
            result = s.value;
            shift_carry = s.carry;
            break;
        }
        case 0x5: { // ADCS: a + b + C — full N/Z/C/V (was update_nz only)
            data_t cin = (xpsr_ & PSR_C) ? 1u : 0u;
            result = a + b + cin;
            auto res = wr(rd, result);
            if (!res) {
                return res;
            }
            set_adc_flags(a, b, cin, result);
            return {};
        }
        case 0x6: { // SBCS: a - b - !C — full N/Z/C/V (was update_nz only)
            data_t cin = (xpsr_ & PSR_C) ? 1u : 0u;
            result = a - b - ((xpsr_ & PSR_C) ? 0u : 1u);
            auto res = wr(rd, result);
            if (!res) {
                return res;
            }
            set_sbc_flags(a, b, cin, result);
            return {};
        }
        case 0x7: { // ROR register
            auto s = barrel_shift(3, a, b & 0xFF, (xpsr_ & PSR_C) != 0);
            result = s.value;
            shift_carry = s.carry;
            break;
        }
        case 0x8:
            update_nz(a & b);
            return {}; // TST
        case 0x9:
            result = -b;
            break; // RSB
        case 0xA:
            update_flags(FlagPostOperation::Sub, a, b, a - b);
            return {};
        case 0xB:
            update_flags(FlagPostOperation::Add, a, b, a + b);
            return {};
        case 0xC:
            result = a | b;
            break;
        case 0xD:
            result = a * b;
            break;
        case 0xE:
            result = a & ~b;
            break;
        case 0xF:
            result = ~b;
            break;
        default:
            return std::unexpected{CPUError::IllegalInstruction};
    }
    auto res = wr(rd, result);
    if (!res) {
        return res;
    }
    update_nz(result);
    if (shift_carry) {
        if (*shift_carry) {
            xpsr_ |= PSR_C;
        } else {
            xpsr_ &= ~PSR_C;
        }
    }
    return {};
}

} // namespace micro_forge::cpu::arm::cortex_m3
