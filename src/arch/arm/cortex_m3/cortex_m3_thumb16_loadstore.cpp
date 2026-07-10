#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "arch/arm/cortex_m3/thumb_fields.hpp"

#include <bit>
#include <expected>

namespace micro_forge::cpu::arm::cortex_m3 {

using namespace thumb;

// ── 16-bit Thumb load/store family handlers ──
// Split out of execute_16bit (cortex_m3_thumb16.cpp). Each returns the result
// of its decode_key-matched block; execute_16bit dispatches. A handler covering
// several decode_key arms re-dispatches internally on decode_key(insn) so the
// boundaries stay identical to the original switch. Bodies moved verbatim;
// rr/wr/br/bw are the shared member accessors.

// LDR literal (PC-relative) (0b01001).
CPU::CPUExpected<void> CortexM3CPU::t16_ldr_literal(uint16_t insn) {
    uint8_t rt = rd8(insn);
    addr_t addr = ((rr(15) + 4) & ~0x3u) + imm8(insn) * 4;
    auto val = br(addr, Width::Word);
    if (!val) {
        return std::unexpected{val.error()};
    }
    return wr(rt, *val);
}

// Load/store register offset (0b01010 STR/STRH/STRB/LDRSB, 0b01011 LDR/LDRH/
// LDRB/LDRSH). op = bits[10:9].
CPU::CPUExpected<void> CortexM3CPU::t16_loadstore_reg_offset(uint16_t insn) {
    uint8_t op = (insn >> 9) & 0x3;
    uint8_t rm = rm3(insn), rn = rn3(insn), rt = rd3(insn);
    addr_t addr = rr(rn) + rr(rm);
    if (decode_key(insn) == 0b01010) {
        // Store register offset (STR/STRH/STRB/LDRSB)
        switch (op) {
            case 0b00: // STR
                return bw(addr, rr(rt), Width::Word);
            case 0b01: // STRH
                return bw(addr, rr(rt) & 0xFFFF, Width::HalfWord);
            case 0b10: // STRB
                return bw(addr, rr(rt) & 0xFF, Width::Byte);
            case 0b11: { // LDRSB
                auto v = br(addr, Width::Byte);
                if (!v) {
                    return std::unexpected{v.error()};
                }
                data_t val = *v;
                if (val & 0x80u) {
                    val |= 0xFFFFFF00u;
                }
                return wr(rt, val);
            }
        }
    }
    // decode_key == 0b01011: Load register offset (LDR/LDRH/LDRB/LDRSH)
    switch (op) {
        case 0b00: { // LDR
            auto v = br(addr, Width::Word);
            if (!v) {
                return std::unexpected{v.error()};
            }
            return wr(rt, *v);
        }
        case 0b01: { // LDRH
            auto v = br(addr, Width::HalfWord);
            if (!v) {
                return std::unexpected{v.error()};
            }
            return wr(rt, *v);
        }
        case 0b10: { // LDRB
            auto v = br(addr, Width::Byte);
            if (!v) {
                return std::unexpected{v.error()};
            }
            return wr(rt, *v);
        }
        case 0b11: { // LDRSH
            auto v = br(addr, Width::HalfWord);
            if (!v) {
                return std::unexpected{v.error()};
            }
            data_t val = *v;
            if (val & 0x8000u) {
                val |= 0xFFFF0000u;
            }
            return wr(rt, val);
        }
    }
    return std::unexpected{CPUError::IllegalInstruction};
}

// Load/store immediate offset (0b01100-0b10001): STR/LDR/STRB/LDRB/STRH/LDRH.
CPU::CPUExpected<void> CortexM3CPU::t16_loadstore_imm_offset(uint16_t insn) {
    switch (decode_key(insn)) {
        // STR word immediate offset
        case 0b01100: {
            addr_t addr = rr(rn3(insn)) + imm5(insn) * 4;
            return bw(addr, rr(rd3(insn)), Width::Word);
        }
        // LDR word immediate offset
        case 0b01101: {
            addr_t addr = rr(rn3(insn)) + imm5(insn) * 4;
            auto v = br(addr, Width::Word);
            if (!v) {
                return std::unexpected{v.error()};
            }
            return wr(rd3(insn), *v);
        }
        // STRB immediate offset
        case 0b01110: {
            addr_t addr = rr(rn3(insn)) + imm5(insn);
            return bw(addr, rr(rd3(insn)) & 0xFF, Width::Byte);
        }
        // LDRB immediate offset
        case 0b01111: {
            addr_t addr = rr(rn3(insn)) + imm5(insn);
            auto v = br(addr, Width::Byte);
            if (!v) {
                return std::unexpected{v.error()};
            }
            return wr(rd3(insn), *v);
        }
        // STRH immediate offset
        case 0b10000: {
            addr_t addr = rr(rn3(insn)) + imm5(insn) * 2;
            return bw(addr, rr(rd3(insn)) & 0xFFFF, Width::HalfWord);
        }
        // LDRH immediate offset
        case 0b10001: {
            addr_t addr = rr(rn3(insn)) + imm5(insn) * 2;
            auto v = br(addr, Width::HalfWord);
            if (!v) {
                return std::unexpected{v.error()};
            }
            return wr(rd3(insn), *v);
        }
    }
    return std::unexpected{CPUError::IllegalInstruction};
}

// Load/store SP-relative (0b10010 STR, 0b10011 LDR).
CPU::CPUExpected<void> CortexM3CPU::t16_loadstore_sp_rel(uint16_t insn) {
    switch (decode_key(insn)) {
        // STR SP-relative
        case 0b10010: {
            addr_t addr = rr(13) + imm8(insn) * 4;
            return bw(addr, rr(rd8(insn)), Width::Word);
        }
        // LDR SP-relative
        case 0b10011: {
            addr_t addr = rr(13) + imm8(insn) * 4;
            auto v = br(addr, Width::Word);
            if (!v) {
                return std::unexpected{v.error()};
            }
            return wr(rd8(insn), *v);
        }
    }
    return std::unexpected{CPUError::IllegalInstruction};
}

// PUSH / ADD SP / SUB SP (0b10110).
// bits[10:9]=00 → ADD/SUB SP, SP, #imm7<<2; bits[10:9]=10 → PUSH.
CPU::CPUExpected<void> CortexM3CPU::t16_push(uint16_t insn) {
    uint8_t sub_op = (insn >> 9) & 0x3;
    if (sub_op == 0b00) {
        // ADD/SUB SP, SP, #imm7<<2
        uint8_t imm7 = insn & 0x7F;
        uint32_t offset = imm7 * 4;
        if (insn & (1 << 7)) {
            // SUB SP
            return write_reg(13, rr(13) - offset);
        }
        // ADD SP
        return write_reg(13, rr(13) + offset);
    }
    // PUSH
    uint8_t rlist = reg_list(insn);
    bool m = m_bit(insn);
    int count = std::popcount(rlist) + (m ? 1 : 0);

    data_t sp = rr(13) - count * 4;
    auto wr = write_reg(13, sp);
    if (!wr) {
        return wr;
    }

    for (int i = 0; i < 8; i++) {
        if (rlist & (1 << i)) {
            auto res = bw(sp, rr(i), Width::Word);
            if (!res) {
                return res;
            }
            sp += 4;
        }
    }
    if (m) {
        return bw(sp, rr(14), Width::Word);
    }
    return {};
}

// POP / Hints (0b10111). bits[10:9]=10 → POP; bits[10:9]=11 → hints/BKPT/IT.
CPU::CPUExpected<void> CortexM3CPU::t16_pop(uint16_t insn) {
    uint8_t sub_op = (insn >> 9) & 0x3;
    if (sub_op == 0b11) {
        // BKPT #imm8 (0xBExx): no debugger attached → HardFault.
        // (Was silently treated as NOP — coverage matrix §2 #6.)
        if ((insn & 0xFF00u) == 0xBE00u) {
            return trigger_hardfault();
        }
        if ((insn & 0xFF00u) == 0xBF00u && (insn & 0xFu) != 0) {
            uint8_t first_cond = (insn >> 4) & 0xFu;
            uint8_t mask = insn & 0xFu;
            if (first_cond == 0xFu) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            int count = 4 - std::countr_zero(static_cast<unsigned>(mask));
            it_conditions_.clear();
            it_condition_pos_ = 0;
            it_conditions_.reserve(static_cast<std::size_t>(count));
            // ARMv7-M IT mask: bit3 is the count sentinel, bits below it encode
            // THEN/ELSE per slot. countr_zero(mask) gives count, but slot i's
            // THEN/ELSE bit is mask[4-i] (slot1→mask[3], slot2→mask[2], …), NOT
            // mask[3-i]. Encoding proof (firstcond[0]=1):
            //   itt ne=0xBF1C mask=0b1100 → slot1 mask[3]=1==1 THEN  → ne
            //   ite ne=0xBF14 mask=0b0100 → slot1 mask[3]=0≠1   ELSE → eq
            //   itee ne=0xBF12 mask=0b0010 → [ne,eq,eq]
            // The old `1u<<(3-slot)` (mask[3-slot]) collapsed itt and ite (both
            // have mask[2]=1) to the same [ne,ne], so ite's ELSE slot executed
            // under the THEN condition — e.g. HAL_GPIO_ReadPin's
            // `ite ne; movne r0,#1; moveq r0,#0` always returned 0.
            for (int slot = 0; slot < count; ++slot) {
                if (slot == 0) {
                    it_conditions_.push_back(first_cond);
                    continue;
                }
                uint8_t bit = 1u << (4 - slot);
                bool then_path = (mask & bit) != 0;
                it_conditions_.push_back(then_path ? first_cond
                                                   : (first_cond ^ 1u));
            }
        }
        // Hints: NOP (0xBF00), YIELD, WFE, WFI, SEV
        return {}; // treat all hints as NOP (old arm ended in break)
    }
    // POP
    uint8_t rlist = reg_list(insn);
    bool m = m_bit(insn);
    data_t sp = rr(13);

    for (int i = 0; i < 8; i++) {
        if (rlist & (1 << i)) {
            auto v = br(sp, Width::Word);
            if (!v) {
                return std::unexpected{v.error()};
            }
            auto res = write_reg(i, *v);
            if (!res) {
                return res;
            }
            sp += 4;
        }
    }
    if (m) {
        auto v = br(sp, Width::Word);
        if (!v) {
            return std::unexpected{v.error()};
        }
        auto res = write_pc(*v);
        if (!res) {
            return res;
        }
        sp += 4;
    }
    auto wr = write_reg(13, sp);
    if (!wr) {
        return wr;
    }
    return {};
}

// STMIA / LDMIA (0b11000 / 0b11001).
CPU::CPUExpected<void> CortexM3CPU::t16_stm_ldm(uint16_t insn) {
    switch (decode_key(insn)) {
        // STMIA Rd!, <reg_list>
        case 0b11000: {
            uint8_t rn = (insn >> 8) & 0x7;
            uint8_t rlist = reg_list(insn);
            data_t addr = rr(rn);
            for (int i = 0; i < 8; i++) {
                if (rlist & (1 << i)) {
                    auto res = bw(addr, rr(i), Width::Word);
                    if (!res) {
                        return res;
                    }
                    addr += 4;
                }
            }
            if (rlist) {
                return write_reg(rn, addr);
            }
            // Empty rlist: STMIA rN!, {} → writeback stores address+0x40
            return write_reg(rn, addr + 0x40);
        }
        // LDMIA Rd!, <reg_list>
        case 0b11001: {
            uint8_t rn = (insn >> 8) & 0x7;
            uint8_t rlist = reg_list(insn);
            data_t addr = rr(rn);
            for (int i = 0; i < 8; i++) {
                if (rlist & (1 << i)) {
                    auto v = br(addr, Width::Word);
                    if (!v) {
                        return std::unexpected{v.error()};
                    }
                    auto res = write_reg(i, *v);
                    if (!res) {
                        return res;
                    }
                    addr += 4;
                }
            }
            if (rlist) {
                // writeback only if Rn is NOT in the lowest-numbered register
                // loaded
                return write_reg(rn, addr);
            }
            // Empty rlist: LDMIA rN!, {} → load PC from [addr], writeback
            // addr+0x40
            return write_reg(rn, addr + 0x40);
        }
    }
    return std::unexpected{CPUError::IllegalInstruction};
}

} // namespace micro_forge::cpu::arm::cortex_m3
