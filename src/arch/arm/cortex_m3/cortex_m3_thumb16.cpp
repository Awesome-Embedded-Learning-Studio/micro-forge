#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "arch/arm/cortex_m3/thumb_fields.hpp"

#include <bit>
#include <expected>

namespace micro_forge::cpu::arm::cortex_m3 {

using namespace thumb;

// ── 16-bit Thumb decode ──

CPU::CPUExpected<void> CortexM3CPU::execute_16bit(uint16_t insn) {

    auto rr = [&](uint8_t idx) -> data_t {
        return regs_.read(idx).value_or(0);
    };
    auto wr = [&](uint8_t idx, data_t val) -> CPUExpected<void> {
        auto res = write_reg(idx, val);
        if (!res) {
            return std::unexpected{res.error()};
        }
        return {};
    };
    // Bus read helper: returns error on failure
    auto br = [&](addr_t addr, Width w) -> CPUExpected<data_t> {
        if (!bus_) {
            record_bus_fault(BusError::InvalidDevice, addr, w);
            return std::unexpected{CPUError::DataAccessFault};
        }
        auto v = bus_->read(addr, w);
        if (!v) {
            record_bus_fault(v.error(), addr, w);
            return std::unexpected{CPUError::DataAccessFault};
        }
        return *v;
    };
    // Bus write helper: returns error on failure
    auto bw = [&](addr_t addr, data_t val, Width w) -> CPUExpected<void> {
        if (!bus_) {
            record_bus_fault(BusError::InvalidDevice, addr, w);
            return std::unexpected{CPUError::DataAccessFault};
        }
        auto v = bus_->write(addr, val, w);
        if (!v) {
            record_bus_fault(v.error(), addr, w);
            return std::unexpected{CPUError::DataAccessFault};
        }
        return {};
    };

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

    // ── Sign/zero extend byte/halfword ──
    if ((insn & 0xFF00u) == 0xB200u) {
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
    }

    // ── Byte-reversal instructions ──
    if ((insn & 0xFF00u) == 0xBA00u) {
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

    switch (decode_key(insn)) {

        // ── Shift immediate (LSL/LSR/ASR) ──
        case 0b00000:
        case 0b00001:
        case 0b00010: {
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
            break;
        }

        // ── Add/subtract register or 3-bit immediate ──
        case 0b00011: {
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

        // ── MOVS Rd, imm8 ──
        case 0b00100: {
            uint8_t rd = rd8(insn);
            data_t val = imm8(insn);
            auto res = wr(rd, val);
            if (!res) {
                return res;
            }
            update_nz(val);
            break;
        }

        // ── CMP Rn, imm8 ──
        case 0b00101: {
            data_t a = rr(rd8(insn));
            data_t b = imm8(insn);
            update_flags(FlagPostOperation::Sub, a, b, a - b);
            break;
        }

        // ── ADDS Rd, imm8 ──
        case 0b00110: {
            uint8_t rd = rd8(insn);
            data_t a = rr(rd), b = imm8(insn);
            data_t result = a + b;
            update_flags(FlagPostOperation::Add, a, b, result);
            return wr(rd, result);
        }

        // ── SUBS Rd, imm8 ──
        case 0b00111: {
            uint8_t rd = rd8(insn);
            data_t a = rr(rd), b = imm8(insn);
            data_t result = a - b;
            update_flags(FlagPostOperation::Sub, a, b, result);
            return wr(rd, result);
        }

        // ── Data processing register OR Special data / BX ──
        case 0b01000: {
            if ((insn >> 10) & 1) {
                // Special data instructions / BX
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
            }
            // Data processing register
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
            break;
        }

        // ── LDR literal (PC-relative) ──
        // Encoding: 0100 1 Rt imm8
        case 0b01001: {
            uint8_t rt = rd8(insn);
            addr_t addr = ((rr(15) + 4) & ~0x3u) + imm8(insn) * 4;
            auto val = br(addr, Width::Word);
            if (!val) {
                return std::unexpected{val.error()};
            }
            return wr(rt, *val);
        }

        // ── Store register offset (STR/STRH/STRB/LDRSB) ──
        // Encoding: 0101 0op Rm Rn Rt, op = bits[10:9]
        case 0b01010: {
            uint8_t op = (insn >> 9) & 0x3;
            uint8_t rm = rm3(insn), rn = rn3(insn), rt = rd3(insn);
            addr_t addr = rr(rn) + rr(rm);
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
            break;
        }

        // ── Load register offset (LDR/LDRH/LDRB/LDRSH) ──
        // Encoding: 0101 1op Rm Rn Rt, op = bits[10:9]
        case 0b01011: {
            uint8_t op = (insn >> 9) & 0x3;
            uint8_t rm = rm3(insn), rn = rn3(insn), rt = rd3(insn);
            addr_t addr = rr(rn) + rr(rm);
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
            break;
        }

        // ── STR word immediate offset ──
        case 0b01100: {
            addr_t addr = rr(rn3(insn)) + imm5(insn) * 4;
            return bw(addr, rr(rd3(insn)), Width::Word);
        }

        // ── LDR word immediate offset ──
        case 0b01101: {
            addr_t addr = rr(rn3(insn)) + imm5(insn) * 4;
            auto v = br(addr, Width::Word);
            if (!v) {
                return std::unexpected{v.error()};
            }
            return wr(rd3(insn), *v);
        }

        // ── STRB immediate offset ──
        case 0b01110: {
            addr_t addr = rr(rn3(insn)) + imm5(insn);
            return bw(addr, rr(rd3(insn)) & 0xFF, Width::Byte);
        }

        // ── LDRB immediate offset ──
        case 0b01111: {
            addr_t addr = rr(rn3(insn)) + imm5(insn);
            auto v = br(addr, Width::Byte);
            if (!v) {
                return std::unexpected{v.error()};
            }
            return wr(rd3(insn), *v);
        }

        // ── STRH immediate offset ──
        case 0b10000: {
            addr_t addr = rr(rn3(insn)) + imm5(insn) * 2;
            return bw(addr, rr(rd3(insn)) & 0xFFFF, Width::HalfWord);
        }

        // ── LDRH immediate offset ──
        case 0b10001: {
            addr_t addr = rr(rn3(insn)) + imm5(insn) * 2;
            auto v = br(addr, Width::HalfWord);
            if (!v) {
                return std::unexpected{v.error()};
            }
            return wr(rd3(insn), *v);
        }

        // ── STR SP-relative ──
        case 0b10010: {
            addr_t addr = rr(13) + imm8(insn) * 4;
            return bw(addr, rr(rd8(insn)), Width::Word);
        }

        // ── LDR SP-relative ──
        case 0b10011: {
            addr_t addr = rr(13) + imm8(insn) * 4;
            auto v = br(addr, Width::Word);
            if (!v) {
                return std::unexpected{v.error()};
            }
            return wr(rd8(insn), *v);
        }

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

        // ── PUSH / ADD SP / SUB SP ──
        // bits[10:9]=00 → ADD/SUB SP, SP, #imm7<<2
        // bits[10:9]=10 → PUSH
        case 0b10110: {
            uint8_t sub_op = (insn >> 9) & 0x3;
            if (sub_op == 0b00) {
                // ADD/SUB SP, SP, #imm7<<2
                uint8_t imm7 = insn & 0x7F;
                uint32_t offset = imm7 * 4;
                if (insn & (1 << 7)) {
                    // SUB SP
                    return write_reg(13, rr(13) - offset);
                } else {
                    // ADD SP
                    return write_reg(13, rr(13) + offset);
                }
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
            break;
        }

        // ── POP / Hints ──
        // bits[10:9]=10 → POP
        // bits[10:9]=11 → Hints (NOP, YIELD, etc.)
        case 0b10111: {
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
                    int count =
                        4 - std::countr_zero(static_cast<unsigned>(mask));
                    it_conditions_.clear();
                    it_condition_pos_ = 0;
                    it_conditions_.reserve(static_cast<std::size_t>(count));
                    for (int slot = 0; slot < count; ++slot) {
                        if (slot == 0) {
                            it_conditions_.push_back(first_cond);
                            continue;
                        }
                        uint8_t bit = 1u << (3 - slot);
                        bool then_path = (mask & bit) != 0;
                        it_conditions_.push_back(then_path ? first_cond
                                                           : (first_cond ^ 1u));
                    }
                }
                // Hints: NOP (0xBF00), YIELD, WFE, WFI, SEV
                break; // treat all hints as NOP
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
            break;
        }

        // ── STMIA Rd!, <reg_list> ──
        case 0b11000: {
            uint8_t rn = (insn >> 8) & 0x7;
            uint8_t rlist = reg_list(insn);
            data_t addr = rr(rn);
            for (int i = 0; i < 8; i++) {
                if (rlist & (1 << i)) {
                    auto res = bw(addr, rr(i), Width::Word);
                    if (!res) return res;
                    addr += 4;
                }
            }
            if (rlist) {
                return write_reg(rn, addr);
            }
            // Empty rlist: STMIA rN!, {} → writeback stores address+0x40
            return write_reg(rn, addr + 0x40);
        }

        // ── LDMIA Rd!, <reg_list> ──
        case 0b11001: {
            uint8_t rn = (insn >> 8) & 0x7;
            uint8_t rlist = reg_list(insn);
            data_t addr = rr(rn);
            for (int i = 0; i < 8; i++) {
                if (rlist & (1 << i)) {
                    auto v = br(addr, Width::Word);
                    if (!v) return std::unexpected{v.error()};
                    auto res = write_reg(i, *v);
                    if (!res) return res;
                    addr += 4;
                }
            }
            if (rlist) {
                // writeback only if Rn is NOT in the lowest-numbered register loaded
                return write_reg(rn, addr);
            }
            // Empty rlist: LDMIA rN!, {} → load PC from [addr], writeback addr+0x40
            return write_reg(rn, addr + 0x40);
        }

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
            break;
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
