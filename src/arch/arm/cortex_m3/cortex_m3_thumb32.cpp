#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "arch/arm/cortex_m3/thumb32_fields.hpp"

#include <bit>
#include <expected>

namespace micro_forge::cpu::arm::cortex_m3 {

using namespace thumb;

// ── 32-bit Thumb-2 decode ──

CPU::CPUExpected<void> CortexM3CPU::execute_32bit(uint16_t hw1, uint16_t hw2) {

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

    // ── BL / BLX ──
    if ((hw1 & 0xF800) == 0xF000 && (hw2 & 0xD000) == 0xD000) {
        uint32_t s = thumb32::s_bit(hw1);
        uint16_t i10 = thumb32::hw1_imm10(hw1);
        uint32_t j1_val = thumb32::j1(hw2);
        uint32_t j2_val = thumb32::j2(hw2);
        uint16_t i11 = thumb32::hw2_imm11(hw2);

        uint32_t i1 = 1u ^ (j1_val ^ s);
        uint32_t i2 = 1u ^ (j2_val ^ s);

        uint32_t offset =
            (s << 24) | (i1 << 23) | (i2 << 22) | (i10 << 12) | (i11 << 1);
        if (s) {
            offset |= 0xFE000000u;
        }

        auto pc_res = read_pc_raw();
        if (!pc_res) {
            return std::unexpected{pc_res.error()};
        }
        data_t next_pc = *pc_res + 4;

        auto lr_res = wr(14, next_pc);
        if (!lr_res) {
            return lr_res;
        }

        bool is_blx = !((hw2 >> 12) & 0x1);
        if (is_blx) {
            return write_reg(15, (*pc_res + 4 + offset) & ~0x1u);
        }
        return write_reg(15, *pc_res + 4 + offset);
    }

    // ── B.W T3 (conditional branch) ──
    if ((hw1 & 0xF800) == 0xF000 && (hw2 & 0xD000) == 0x8000 &&
        (((hw1 >> 6) & 0xFu) < 0xEu)) {
        uint8_t c = (hw1 >> 6) & 0xFu;
        if (!condition_need_execute(c)) {
            return {};
        }

        uint32_t s = (hw1 >> 10) & 0x1u;
        uint32_t j1_val = thumb32::j1(hw2);
        uint32_t j2_val = thumb32::j2(hw2);
        uint32_t imm6 = hw1 & 0x3Fu;
        uint32_t imm11 = thumb32::hw2_imm11(hw2);
        uint32_t offset = (s << 20) | (j2_val << 19) | (j1_val << 18) |
                          (imm6 << 12) | (imm11 << 1);
        if (s) {
            offset |= 0xFFE00000u;
        }

        auto pc_res = read_pc_raw();
        if (!pc_res) {
            return std::unexpected{pc_res.error()};
        }
        return write_reg(15, *pc_res + 4 + offset);
    }

    // ── B.W T4 (unconditional branch) ──
    if ((hw1 & 0xF800) == 0xF000 && (hw2 & 0xD000) == 0x9000) {
        uint32_t s = thumb32::s_bit(hw1);
        uint16_t i10 = thumb32::hw1_imm10(hw1);
        uint32_t j1_val = thumb32::j1(hw2);
        uint32_t j2_val = thumb32::j2(hw2);
        uint16_t i11 = thumb32::hw2_imm11(hw2);

        uint32_t i1 = 1u ^ (j1_val ^ s);
        uint32_t i2 = 1u ^ (j2_val ^ s);

        uint32_t offset =
            (s << 24) | (i1 << 23) | (i2 << 22) | (i10 << 12) | (i11 << 1);
        if (s) {
            offset |= 0xFE000000u;
        }

        auto pc_res = read_pc_raw();
        if (!pc_res) {
            return std::unexpected{pc_res.error()};
        }
        return write_reg(15, *pc_res + 4 + offset);
    }

    // ── MOVW ──
    if ((hw1 & 0xFBF0) == 0xF240) {
        return wr(thumb32::hw2_rd4(hw2), thumb32::decode_imm16(hw1, hw2));
    }

    // ── MOVT ──
    if ((hw1 & 0xFBF0) == 0xF2C0) {
        uint16_t imm16 = thumb32::decode_imm16(hw1, hw2);
        uint8_t rd = thumb32::hw2_rd4(hw2);
        data_t val =
            (rr(rd) & 0x0000FFFFu) | (static_cast<data_t>(imm16) << 16);
        return wr(rd, val);
    }

    // ── DMB / DSB / ISB ──
    if (hw1 == 0xF3BF && (hw2 & 0xFF0Fu) == 0x8F0Fu) {
        uint8_t option = hw2 & 0xFu;
        uint8_t op = (hw2 >> 4) & 0xFu;
        if (option != 0xFu || (op != 0x4u && op != 0x5u && op != 0x6u)) {
            return std::unexpected{CPUError::IllegalInstruction};
        }
        return {};
    }

    auto read_special = [&]() -> data_t {
        uint8_t sysm = hw2 & 0xFFu;
        switch (sysm) {
            case 0x00:
                return xpsr_ & (PSR_N | PSR_Z | PSR_C | PSR_V);
            case 0x08:
                return msp_;
            case 0x09:
                return psp_;
            case 0x10:
                return primask_;
            case 0x11:
                return basepri_;
            case 0x13:
                return faultmask_;
            case 0x14:
                return control_;
            default:
                return 0;
        }
    };

    auto write_special = [&](data_t value) -> CPUExpected<void> {
        uint8_t sysm = hw2 & 0xFFu;
        switch (sysm) {
            case 0x00:
                xpsr_ = (xpsr_ & ~(PSR_N | PSR_Z | PSR_C | PSR_V)) |
                        (value & (PSR_N | PSR_Z | PSR_C | PSR_V)) | PSR_T;
                return {};
            case 0x08:
                msp_ = value & ~0x3u;
                if (in_handler_mode_ || !(control_ & 0x2u)) {
                    return write_reg(13, msp_);
                }
                return {};
            case 0x09:
                psp_ = value & ~0x3u;
                if (!in_handler_mode_ && (control_ & 0x2u)) {
                    return write_reg(13, psp_);
                }
                return {};
            case 0x10:
                primask_ = value & 1u;
                return {};
            case 0x11:
                basepri_ = value & 0xFFu;
                return {};
            case 0x13:
                faultmask_ = value & 1u;
                return {};
            case 0x14: {
                control_ = value & 0x3u;
                data_t active_sp =
                    (!in_handler_mode_ && (control_ & 0x2u)) ? psp_ : msp_;
                return write_reg(13, active_sp);
            }
            default:
                return std::unexpected{CPUError::IllegalInstruction};
        }
    };

    // ── MRS ──
    if ((hw1 & 0xFFF0) == 0xF3E0 && (hw2 & 0xF000) == 0x8000) {
        return wr(thumb32::hw2_rd4(hw2), read_special());
    }

    // ── MSR ──
    if ((hw1 & 0xFFF0) == 0xF380 && (hw2 & 0xFF00) == 0x8800) {
        return write_special(rr(hw1 & 0xFu));
    }

    // ── BFI / BFC ──
    if ((hw1 & 0xFB70u) == 0xF360u) {
        uint8_t rn = hw1 & 0xFu;
        uint8_t rd = (hw2 >> 8) & 0xFu;
        uint8_t lsb = (((hw2 >> 12) & 0x7u) << 2) | ((hw2 >> 6) & 0x3u);
        uint8_t msb = hw2 & 0x1Fu;
        if (msb < lsb) {
            return std::unexpected{CPUError::IllegalInstruction};
        }
        uint32_t width = static_cast<uint32_t>(msb - lsb + 1);
        uint32_t field_mask = width == 32 ? 0xFFFFFFFFu : ((1u << width) - 1u);
        uint32_t mask = field_mask << lsb;
        uint32_t src = (rn == 15) ? 0u : (rr(rn) << lsb);
        return wr(rd, (rr(rd) & ~mask) | (src & mask));
    }

    // ── SBFX / UBFX ──
    if ((hw1 & 0xFB70u) == 0xF340u || (hw1 & 0xFB70u) == 0xF3C0u) {
        bool is_unsigned = (hw1 & 0x0080u) != 0;
        uint8_t rn = hw1 & 0xFu;
        uint8_t rd = (hw2 >> 8) & 0xFu;
        uint8_t lsb = (((hw2 >> 12) & 0x7u) << 2) | ((hw2 >> 6) & 0x3u);
        uint8_t width = (hw2 & 0x1Fu) + 1u;
        if (lsb + width > 32) {
            return std::unexpected{CPUError::IllegalInstruction};
        }
        uint32_t raw = rr(rn) >> lsb;
        uint32_t mask = width == 32 ? 0xFFFFFFFFu : ((1u << width) - 1u);
        uint32_t result = raw & mask;
        if (!is_unsigned && width < 32 && (result & (1u << (width - 1u)))) {
            result |= ~mask;
        }
        return wr(rd, result);
    }

    // ── Add/subtract (plain imm12): insn[25]=1 (hw1[9]) ──
    // addw/subw (S=0) and adds.w/subs.w (S=1). imm12 = i:imm3:imm8 packed
    // PLAIN (not Thumb2ExpandImm). op = hw1[8:5]: 0000=ADD, 0101=SUB; S=hw1[4].
    // Modified-immediate (insn[25]=0, e.g. cmp.w #0x110000) keeps its own form.
    if ((hw1 & 0xF800) == 0xF000 && (hw1 & 0x0200) != 0 &&
        (hw2 & 0x8000) == 0) {
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
    if ((hw1 & 0xF800) == 0xF000 && (hw1 & 0x0200) == 0 &&
        (hw2 & 0x8000) == 0) {
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

    // ── Load/Store single (immediate): str/ldr/strb/ldrb/strh/ldrh .W ──
    // hw1[7] selects the immediate form:
    //   1 → imm12 offset (T2/T3): addr = Rn + imm12, no writeback.
    //   0 → imm8 with addressing modes, op = hw2[11:8]:
    //       0=offset+, C=offset-, B=post+, 9=post-, F=pre+, D=pre-.
    if ((hw1 & 0xFF00) == 0xF800) {
        uint8_t rn = hw1 & 0xF;
        bool load = (hw1 >> 4) & 1;
        uint8_t size = (hw1 >> 5) & 0x3;
        uint8_t rt = (hw2 >> 12) & 0xF;
        uint32_t rn_val = rr(rn);
        Width width;
        switch (size) {
            case 0:
                width = Width::Byte;
                break;
            case 1:
                width = Width::HalfWord;
                break;
            case 2:
                width = Width::Word;
                break;
            default:
                return std::unexpected{CPUError::IllegalInstruction};
        }

        // ── LDR.W (literal): Rn == PC ──
        // `ldr.w Rt, [pc, #imm12]` — PC-relative literal pool load (compiled
        // `LDR Rd, =const`). addr = Align(PC+4, 4) + imm12, no writeback.
        // Store-to-PC-relative is UNDEFINED → rejected for !load.
        if (rn == 15) {
            if (!load) {
                return std::unexpected{CPUError::IllegalInstruction};
            }
            uint32_t imm12 = hw2 & 0xFFFu;
            auto pc_res = read_pc_raw();
            if (!pc_res) {
                return std::unexpected{pc_res.error()};
            }
            addr_t addr = ((*pc_res + 4) & ~0x3u) + imm12;
            auto r = br(addr, width);
            if (!r) {
                return std::unexpected{r.error()};
            }
            return wr(rt, *r);
        }

        // Resolve effective address + optional writeback per immediate form.
        addr_t addr = 0;
        bool writeback = false;
        data_t wb_val = 0;
        if ((hw1 >> 7) & 1) {
            // imm12 offset form (no writeback).
            addr = rn_val + (hw2 & 0xFFFu);
        } else {
            uint8_t op = (hw2 >> 8) & 0xF;
            uint32_t imm8 = hw2 & 0xFF;
            switch (op) {
                case 0x0: // [Rn, #+imm8]
                    addr = rn_val + imm8;
                    break;
                case 0xC: // [Rn, #-imm8]
                    addr = rn_val - imm8;
                    break;
                case 0xB: // [Rn], #+imm8  (post-index)
                    addr = rn_val;
                    wb_val = rn_val + imm8;
                    writeback = true;
                    break;
                case 0x9: // [Rn], #-imm8  (post-index)
                    addr = rn_val;
                    wb_val = rn_val - imm8;
                    writeback = true;
                    break;
                case 0xF: // [Rn, #+imm8]! (pre-index)
                    addr = rn_val + imm8;
                    wb_val = addr;
                    writeback = true;
                    break;
                case 0xD: // [Rn, #-imm8]! (pre-index)
                    addr = rn_val - imm8;
                    wb_val = addr;
                    writeback = true;
                    break;
                default:
                    return std::unexpected{CPUError::IllegalInstruction};
            }
        }

        if (load) {
            auto v = br(addr, width);
            if (!v) {
                return std::unexpected{v.error()};
            }
            auto w = wr(rt, *v);
            if (!w) {
                return w;
            }
        } else {
            auto w = bw(addr, rr(rt), width);
            if (!w) {
                return w;
            }
        }
        if (writeback) {
            return wr(rn, wb_val);
        }
        return {};
    }

    // ── UDIV / SDIV ──
    if ((hw1 & 0xFFD0) == 0xFB90 && (hw2 & 0xF0F0) == 0xF0F0) {
        uint8_t rn = hw1 & 0xF;
        uint8_t rm = hw2 & 0xF;
        uint8_t rd = (hw2 >> 8) & 0xF;
        bool is_signed = (hw1 & 0x0020u) == 0;
        if (is_signed) {
            int32_t a = static_cast<int32_t>(rr(rn));
            int32_t b = static_cast<int32_t>(rr(rm));
            if (b == 0) {
                return wr(rd, 0);
            }
            return wr(rd, static_cast<uint32_t>(a / b));
        }
        uint32_t a = rr(rn);
        uint32_t b = rr(rm);
        if (b == 0) {
            return wr(rd, 0);
        }
        return wr(rd, a / b);
    }

    // ── MLA / MLS ──
    if ((hw1 & 0xFFF0u) == 0xFB00u &&
        ((hw2 & 0x00F0u) == 0x0000u || (hw2 & 0x00F0u) == 0x0010u)) {
        uint8_t rn = hw1 & 0xFu;
        uint8_t rm = hw2 & 0xFu;
        uint8_t rd = (hw2 >> 8) & 0xFu;
        uint8_t ra = (hw2 >> 12) & 0xFu;
        uint32_t product = rr(rn) * rr(rm);
        uint32_t result =
            (hw2 & 0x0010u) ? (rr(ra) - product) : (product + rr(ra));
        return wr(rd, result);
    }

    // ── SMULL / UMULL ──
    if (((hw1 & 0xFFF0u) == 0xFB80u || (hw1 & 0xFFF0u) == 0xFBA0u) &&
        (hw2 & 0x00F0u) == 0x0000u) {
        uint8_t rn = hw1 & 0xFu;
        uint8_t rm = hw2 & 0xFu;
        uint8_t rdlo = (hw2 >> 12) & 0xFu;
        uint8_t rdhi = (hw2 >> 8) & 0xFu;
        uint64_t result;
        if ((hw1 & 0xFFF0u) == 0xFB80u) {
            result = static_cast<uint64_t>(
                static_cast<int64_t>(static_cast<int32_t>(rr(rn))) *
                static_cast<int64_t>(static_cast<int32_t>(rr(rm))));
        } else {
            result =
                static_cast<uint64_t>(rr(rn)) * static_cast<uint64_t>(rr(rm));
        }
        auto lo = wr(rdlo, static_cast<uint32_t>(result));
        if (!lo) {
            return lo;
        }
        return wr(rdhi, static_cast<uint32_t>(result >> 32));
    }

    // ── Data processing (shifted register): AND, ORR, EOR, ADD, SUB, etc. ──
    if ((hw1 & 0xFE00) == 0xEA00 && (hw2 & 0x8000) == 0) {
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
    if ((hw1 & 0xFF00) == 0xFA00 && (hw2 & 0xF0F0) == 0xF000) {
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

    // ── TBB / TBH (Table Branch) ──
    if ((hw1 & 0xFFF0) == 0xE8D0 && (hw2 & 0xF0F0) == 0xF000) {
        uint8_t rn = hw1 & 0xF;
        uint8_t rm = hw2 & 0xF;
        bool H = (hw2 >> 4) & 1;

        uint32_t pc_val = rr(15) + 4;
        uint32_t base = (rn == 15) ? pc_val : rr(rn);
        uint32_t index = (rm == 15) ? 0u : rr(rm);

        uint32_t halfwords;
        if (H) {
            auto v = br(base + index * 2, Width::HalfWord);
            if (!v) {
                return std::unexpected{v.error()};
            }
            halfwords = *v;
        } else {
            auto v = br(base + index, Width::Byte);
            if (!v) {
                return std::unexpected{v.error()};
            }
            halfwords = *v;
        }

        addr_t target = pc_val + halfwords * 2;
        return write_pc(target);
    }

    // ── STRD / LDRD (Store/Load Dual, immediate offset) ──
    if ((hw1 & 0xFE40) == 0xE840) {
        bool P = (hw1 >> 8) & 1;
        bool U = (hw1 >> 7) & 1;
        bool W = (hw1 >> 5) & 1;
        bool L = (hw1 >> 4) & 1;
        uint8_t rn = hw1 & 0xF;
        uint8_t rt = (hw2 >> 12) & 0xF;
        uint8_t rt2 = (hw2 >> 8) & 0xF;
        uint32_t offset = static_cast<uint32_t>((hw2 & 0xFF)) * 4;

        uint32_t rn_val = rr(rn);
        addr_t offset_addr = U ? (rn_val + offset) : (rn_val - offset);
        addr_t addr = P ? offset_addr : rn_val;

        if (L) {
            auto v1 = br(addr, Width::Word);
            if (!v1) {
                return std::unexpected{v1.error()};
            }
            auto v2 = br(addr + 4, Width::Word);
            if (!v2) {
                return std::unexpected{v2.error()};
            }
            auto w1 = wr(rt, *v1);
            if (!w1) {
                return w1;
            }
            auto w2 = wr(rt2, *v2);
            if (!w2) {
                return w2;
            }
        } else {
            auto w1 = bw(addr, rr(rt), Width::Word);
            if (!w1) {
                return w1;
            }
            auto w2 = bw(addr + 4, rr(rt2), Width::Word);
            if (!w2) {
                return w2;
            }
        }

        if (W) {
            return wr(rn, offset_addr);
        }
        return {};
    }

    // ── STM / LDM (Store/Load Multiple) ──
    if ((hw1 & 0xFE40) == 0xE800) {
        bool U = (hw1 >> 7) & 1;
        bool W = (hw1 >> 5) & 1;
        bool L = (hw1 >> 4) & 1;
        uint8_t rn = hw1 & 0xF;
        uint16_t rlist = hw2;

        int count = std::popcount(rlist);
        if (count == 0) {
            return std::unexpected{CPUError::IllegalInstruction};
        }

        uint32_t rn_val = rr(rn);
        bool decrement = !U;
        addr_t start_addr =
            decrement ? rn_val - static_cast<uint32_t>(count * 4) : rn_val;
        addr_t addr = start_addr;

        if (L) {
            for (int i = 0; i < 16; i++) {
                if (rlist & (1 << i)) {
                    auto v = br(addr, Width::Word);
                    if (!v) {
                        return std::unexpected{v.error()};
                    }
                    if (i == 15) {
                        auto w = write_pc(*v);
                        if (!w) {
                            return w;
                        }
                    } else {
                        auto w = wr(i, *v);
                        if (!w) {
                            return w;
                        }
                    }
                    addr += 4;
                }
            }
        } else {
            for (int i = 0; i < 16; i++) {
                if (rlist & (1 << i)) {
                    data_t val = (i == 15) ? (rr(15) + 4) : rr(i);
                    auto w = bw(addr, val, Width::Word);
                    if (!w) {
                        return w;
                    }
                    addr += 4;
                }
            }
        }

        if (W) {
            uint32_t new_rn = decrement
                                  ? rn_val - static_cast<uint32_t>(count * 4)
                                  : rn_val + static_cast<uint32_t>(count * 4);
            return wr(rn, new_rn);
        }
        return {};
    }

    return std::unexpected{CPUError::IllegalInstruction};
}

} // namespace micro_forge::cpu::arm::cortex_m3
