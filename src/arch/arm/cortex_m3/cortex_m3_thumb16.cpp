#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "arch/arm/cortex_m3/thumb_fields.hpp"

#include <expected>

namespace micro_forge::cpu::arm::cortex_m3 {

using namespace thumb;

// ── 16-bit Thumb decode ──
//
// This is the dispatcher. CPS and CBZ run inline first, then the extend/reverse
// prefix probes — their encodings collide with decode_key arms, so they MUST be
// matched before the switch (order is load-bearing; see OPEN GOTCHAS). The
// decode_key switch then delegates each family to a t16_* handler in
// cortex_m3_thumb16_{dataproc,loadstore}.cpp. ADR/ADD-SP/B<cond>/B stay inline
// (small, neither dataproc nor load/store). rr/wr/br/bw are the shared member
// accessors (cortex_m3_thumb32.cpp).

CPU::CPUExpected<void> CortexM3CPU::execute_16bit(uint16_t insn) {
    // ── CPS effect {i,f}: CPSIE (enable) / CPSID (disable) ──
    // 0xB66x (CPSIE) / 0xB67x (CPSID); bit4 = 0/1 (enable/disable),
    // bit1 = i (PRIMASK), bit0 = f (FAULTMASK). The old 0xFFF0 mask ignored
    // bit[1:0], so cpsie/cpsid f silently acted on PRIMASK, not FAULTMASK.
    if ((insn & 0xFFE0u) == 0xB660u) {
        bool disable = (insn >> 4) & 1u;
        if (insn & 0x2u) { // i → PRIMASK
            if (disable) {
                primask_ |= 1u;
            } else {
                primask_ &= ~1u;
            }
        }
        if (insn & 0x1u) { // f → FAULTMASK
            if (disable) {
                faultmask_ |= 1u;
            } else {
                faultmask_ &= ~1u;
            }
        }
        return {};
    }

    // ── Compare and branch on zero/non-zero (CBZ / CBNZ) ──
    // Encoding: 1011 op 0 i 1 imm5 Rn
    if ((insn & 0xF500u) == 0xB100u) {
        uint8_t rn = insn & 0x7u;
        bool non_zero = insn & 0x0800u;
        uint32_t offset =
            (((insn >> 9) & 0x1u) << 6) | (((insn >> 3) & 0x1Fu) << 1);
        bool branch = non_zero ? (rr(rn) != 0) : (rr(rn) == 0);
        if (branch) {
            auto pc_res = read_pc_raw();
            if (!pc_res) {
                return std::unexpected{pc_res.error()};
            }
            return write_reg(15, *pc_res + 4 + offset);
        }
        return {};
    }

    // ── Prefix probes BEFORE the decode_key switch (load-bearing order) ──
    if ((insn & 0xFF00u) == 0xB200u) {
        return t16_extend(insn); // SXTH/SXTB/UXTH/UXTB
    }
    if ((insn & 0xFF00u) == 0xBA00u) {
        return t16_reverse(insn); // REV/REV16/REVSH
    }

    switch (decode_key(insn)) {
        // ── Shift immediate (LSL/LSR/ASR) ──
        case 0b00000:
        case 0b00001:
        case 0b00010:
            return t16_shift_imm(insn);

        // ── Add/subtract register or 3-bit immediate ──
        case 0b00011:
            return t16_addsub_reg3(insn);

        // ── MOVS/CMP/ADDS/SUBS Rd, imm8 ──
        case 0b00100:
        case 0b00101:
        case 0b00110:
        case 0b00111:
            return t16_imm8_dataops(insn);

        // ── Special data / BX (bit10=1) OR Data processing register (bit10=0)
        // ──
        case 0b01000:
            return ((insn >> 10) & 1) ? t16_special_bx(insn)
                                      : t16_dataproc_reg(insn);

        // ── LDR literal (PC-relative) ──
        case 0b01001:
            return t16_ldr_literal(insn);

        // ── Load/store register offset ──
        case 0b01010:
        case 0b01011:
            return t16_loadstore_reg_offset(insn);

        // ── Load/store immediate offset ──
        case 0b01100:
        case 0b01101:
        case 0b01110:
        case 0b01111:
        case 0b10000:
        case 0b10001:
            return t16_loadstore_imm_offset(insn);

        // ── Load/store SP-relative ──
        case 0b10010:
        case 0b10011:
            return t16_loadstore_sp_rel(insn);

        // ── ADR / ADD Rd, SP, #imm8*4 ──
        // 1010 0 ddd iiiiiiii → ADR: Rd = Align(PC+4, 4) + imm*4
        // 1010 1 ddd iiiiiiii → ADD Rd, SP, #imm*4
        case 0b10100: { // ADR (PC-relative)
            uint8_t rd = rd8(insn);
            auto pc_res = read_pc_raw();
            if (!pc_res) {
                return std::unexpected{pc_res.error()};
            }
            uint32_t base = (*pc_res + 4u) & ~3u;
            return wr(rd, base + imm8(insn) * 4u);
        }
        case 0b10101: { // ADD Rd, SP, #imm*4
            return wr(rd8(insn), rr(13) + imm8(insn) * 4u);
        }

        // ── PUSH / POP ──
        case 0b10110:
            return t16_push(insn);
        case 0b10111:
            return t16_pop(insn);

        // ── STMIA / LDMIA ──
        case 0b11000:
        case 0b11001:
            return t16_stm_ldm(insn);

        // ── Conditional branch B<cond> ──
        case 0b11010:
        case 0b11011: {
            uint8_t c = cond(insn);
            if (c == 0xE) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            if (c == 0xF) {
                auto pc_res = read_pc_raw();
                if (!pc_res) {
                    return std::unexpected{pc_res.error()};
                }
                auto pc_write = write_reg(15, *pc_res + 2);
                if (!pc_write) {
                    return pc_write;
                }
                return interrupt_entry_system(11);
            }
            if (condition_need_execute(c)) {
                int32_t offset = static_cast<int8_t>(imm8(insn));
                offset <<= 1;
                auto pc_res = read_pc_raw();
                if (!pc_res) {
                    return std::unexpected{pc_res.error()};
                }
                return write_reg(15, *pc_res + 4 + offset);
            }
            break; // condition false → fall through to return {}
        }

        // ── B unconditional ──
        case 0b11100: {
            int32_t offset = static_cast<int16_t>(imm11(insn) << 5) >> 5;
            offset <<= 1;
            auto pc_res = read_pc_raw();
            if (!pc_res) {
                return std::unexpected{pc_res.error()};
            }
            return write_reg(15, *pc_res + 4 + offset);
        }

        default:
            return std::unexpected{CPUError::IllegalInstruction};
    }
    return {};
}

} // namespace micro_forge::cpu::arm::cortex_m3
