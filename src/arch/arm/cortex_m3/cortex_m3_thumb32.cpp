#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "arch/arm/cortex_m3/thumb32_fields.hpp"

#include <bit>
#include <expected>

namespace micro_forge::cpu::arm::cortex_m3 {

using namespace thumb;

// ── Operand helpers shared across the 32-bit Thumb-2 handlers ──
// Promoted from execute_32bit-local lambdas so the family handlers split into
// cortex_m3_thumb32_{loadstore,dataproc}.cpp can use them. Bodies unchanged.
data_t CortexM3CPU::rr(uint8_t idx) {
    return regs_.unchecked(idx);
}
CPU::CPUExpected<void> CortexM3CPU::wr(uint8_t idx, data_t val) {
    auto res = write_reg(idx, val);
    if (!res) {
        return std::unexpected{res.error()};
    }
    return {};
}
CPU::CPUExpected<data_t> CortexM3CPU::br(addr_t addr, Width w) {
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
}
CPU::CPUExpected<void> CortexM3CPU::bw(addr_t addr, data_t val, Width w) {
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
}

// ── 32-bit Thumb-2 family matchers + dispatch tables ──
// Pure hw1/hw2 predicates, one per decode family. The tables in execute_32bit
// pair each matcher with its handler IN MATCH-PRIORITY ORDER (load-bearing:
// SSAT before dataproc-imm, LDREX/TBB before STRD) so dispatch priority is
// visible and each row is independently testable. See CODING-TASTE §4.
namespace {
struct T32Dispatch {
    bool (*match)(uint16_t hw1, uint16_t hw2);
    CPU::CPUExpected<void> (CortexM3CPU::*handler)(uint16_t hw1, uint16_t hw2);
};
bool t32m_ssat_usat(uint16_t hw1, uint16_t) {
    return (hw1 & 0xFFD0u) == 0xF300u || (hw1 & 0xFFD0u) == 0xF380u;
}
bool t32m_addsub_imm(uint16_t hw1, uint16_t hw2) {
    return (hw1 & 0xF800u) == 0xF000u && (hw1 & 0x0200u) != 0u &&
           (hw2 & 0x8000u) == 0u;
}
bool t32m_dataproc_imm(uint16_t hw1, uint16_t hw2) {
    return (hw1 & 0xF800u) == 0xF000u && (hw1 & 0x0200u) == 0u &&
           (hw2 & 0x8000u) == 0u;
}
bool t32m_loadstore_single(uint16_t hw1, uint16_t) {
    return (hw1 & 0xFE00u) == 0xF800u;
}
bool t32m_dataproc_reg(uint16_t hw1, uint16_t hw2) {
    return (hw1 & 0xFE00u) == 0xEA00u && (hw2 & 0x8000u) == 0u;
}
bool t32m_shift_reg(uint16_t hw1, uint16_t hw2) {
    return (hw1 & 0xFF00u) == 0xFA00u && (hw2 & 0xF0F0u) == 0xF000u;
}
bool t32m_misc_reverse(uint16_t hw1, uint16_t hw2) {
    return (hw1 & 0xFF00u) == 0xFA00u && (hw2 & 0xF000u) == 0xF000u &&
           (hw2 & 0x00F0u) != 0u;
}
bool t32m_tbb_tbh(uint16_t hw1, uint16_t hw2) {
    return (hw1 & 0xFFF0u) == 0xE8D0u && (hw2 & 0xFFE0u) == 0xF000u;
}
bool t32m_ldrex_strex(uint16_t hw1, uint16_t) {
    return (hw1 & 0xFF60u) == 0xE840u;
}
bool t32m_strd_ldrd(uint16_t hw1, uint16_t) {
    return (hw1 & 0xFE40u) == 0xE840u;
}
bool t32m_stm_ldm(uint16_t hw1, uint16_t) {
    return (hw1 & 0xFE40u) == 0xE800u;
}
} // namespace

// ── 32-bit Thumb-2 decode ──
//
// This is the dispatcher: each mask is checked in the same order as before the
// split; the large data-processing and load/store families delegate to the
// t32_* handlers in cortex_m3_thumb32_{dataproc,loadstore}.cpp. Branches,
// MOVW/MOVT, MSR/MRS, bitfield, multiply/divide stay inline.

CPU::CPUExpected<void> CortexM3CPU::execute_32bit(uint16_t hw1, uint16_t hw2) {

    auto read_special = [&]() -> data_t {
        uint8_t sysm = hw2 & 0xFFu;
        switch (sysm) {
            case 0x00:
                return xpsr_ & (PSR_N | PSR_Z | PSR_C | PSR_V | PSR_Q);
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
                xpsr_ = (xpsr_ & ~(PSR_N | PSR_Z | PSR_C | PSR_V | PSR_Q)) |
                        (value & (PSR_N | PSR_Z | PSR_C | PSR_V | PSR_Q)) |
                        PSR_T;
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

    // ── DMB / DSB / ISB / CLREX ──
    if (hw1 == 0xF3BF && (hw2 & 0xFF0Fu) == 0x8F0Fu) {
        uint8_t option = hw2 & 0xFu;
        uint8_t op = (hw2 >> 4) & 0xFu;
        // op=4 DSB, 5 DMB, 6 ISB; op=2 CLREX (no-op on a single-core sim).
        if (option != 0xFu ||
            (op != 0x2u && op != 0x4u && op != 0x5u && op != 0x6u)) {
            return std::unexpected{CPUError::IllegalInstruction};
        }
        return {};
    }

    // ── NOP.W / YIELD.W / SEV.W (T4 hints, f3af 80xx) ── no-op on this sim.
    if (hw1 == 0xF3AF && (hw2 & 0xF000u) == 0x8000u) {
        return {};
    }

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

    // ── Data-side family dispatch (table-driven; table order = priority) ──
    // SSAT/USAT must precede dataproc-imm (both match 0xF3xx with insn[25]=0);
    // addsub-imm (insn[25]=1) and dataproc-imm (insn[25]=0) split on hw1[9].
    // loadstore_single (0xFE00) covers 0xF8xx unsigned + 0xF9xx signed forms.
    static const T32Dispatch t32_data_families[] = {
        {t32m_ssat_usat, &CortexM3CPU::t32_ssat_usat},
        {t32m_addsub_imm, &CortexM3CPU::t32_addsub_plain_imm},
        {t32m_dataproc_imm, &CortexM3CPU::t32_dataproc_imm},
        {t32m_loadstore_single, &CortexM3CPU::t32_loadstore_single},
    };
    for (const auto& entry : t32_data_families) {
        if (entry.match(hw1, hw2)) {
            return (this->*entry.handler)(hw1, hw2);
        }
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
                // Cortex-M3 SDIV/0 (CCR.DIV_0_TRP==0, reset default) returns 0
                // for BOTH signs — confirmed vs qemu-system-arm (mps2-an385);
                // see scripts/qemu_sdiv_oracle.sh + notes 017. (DIV_0_TRP==1
                // UsageFault is a configurable-fault feature, not modelled.)
                return wr(rd, 0u);
            }
            // INT_MIN / -1 is signed overflow (UB in C); ARMv7-M saturates to
            // INT_MIN. Guard before the C division to avoid UB.
            if (static_cast<uint32_t>(a) == 0x80000000u && b == -1) {
                return wr(rd, 0x80000000u);
            }
            return wr(rd, static_cast<uint32_t>(a / b));
        }
        uint32_t a = rr(rn);
        uint32_t b = rr(rm);
        if (b == 0) {
            // UDIV/0 → 0 (DIV_0_TRP==0 default).
            return wr(rd, 0u);
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
        // MUL.W (Ra=15) is MLA/MLS without an accumulator; rr(15) would fold
        // the raw PC into the product. Treat Ra=15 as "no accumulate".
        uint32_t acc = (ra == 15) ? 0u : rr(ra);
        uint32_t result = (hw2 & 0x0010u) ? (acc - product) : (product + acc);
        return wr(rd, result);
    }

    // ── SMULL/UMULL (no accumulate) and SMLAL/UMLAL (accumulate) ──
    uint16_t mp_hw1 = hw1 & 0xFFF0u;
    if ((mp_hw1 == 0xFB80u || mp_hw1 == 0xFBA0u || mp_hw1 == 0xFBC0u ||
         mp_hw1 == 0xFBE0u) &&
        (hw2 & 0x00F0u) == 0x0000u) {
        uint8_t rn = hw1 & 0xFu;
        uint8_t rm = hw2 & 0xFu;
        uint8_t rdlo = (hw2 >> 12) & 0xFu;
        uint8_t rdhi = (hw2 >> 8) & 0xFu;
        bool accumulate = (mp_hw1 == 0xFBC0u || mp_hw1 == 0xFBE0u);
        bool is_signed = (mp_hw1 == 0xFB80u || mp_hw1 == 0xFBC0u);
        uint64_t product =
            is_signed
                ? static_cast<uint64_t>(
                      static_cast<int64_t>(static_cast<int32_t>(rr(rn))) *
                      static_cast<int64_t>(static_cast<int32_t>(rr(rm))))
                : static_cast<uint64_t>(rr(rn)) * static_cast<uint64_t>(rr(rm));
        // SMLAL/UMLAL accumulate the existing RdHi:RdLo (read before write).
        uint64_t result =
            accumulate
                ? product + (static_cast<uint64_t>(rr(rdhi)) << 32) + rr(rdlo)
                : product;
        auto lo = wr(rdlo, static_cast<uint32_t>(result));
        if (!lo) {
            return lo;
        }
        return wr(rdhi, static_cast<uint32_t>(result >> 32));
    }

    // ── Register-side family dispatch (table-driven; table order = priority)
    // ── misc_reverse (op2≠0) must follow shift_reg (op2==0); LDREX (0xFF60)
    // and TBB/TBH (0xE8D0) must precede STRD/LDRD (0xFE40) — STRD's mask is
    // wider and would swallow them.
    static const T32Dispatch t32_reg_families[] = {
        {t32m_dataproc_reg, &CortexM3CPU::t32_dataproc_reg},
        {t32m_shift_reg, &CortexM3CPU::t32_shift_reg},
        {t32m_misc_reverse, &CortexM3CPU::t32_misc_reverse},
        {t32m_tbb_tbh, &CortexM3CPU::t32_tbb_tbh},
        {t32m_ldrex_strex, &CortexM3CPU::t32_ldrex_strex},
        {t32m_strd_ldrd, &CortexM3CPU::t32_strd_ldrd},
        {t32m_stm_ldm, &CortexM3CPU::t32_stm_ldm},
    };
    for (const auto& entry : t32_reg_families) {
        if (entry.match(hw1, hw2)) {
            return (this->*entry.handler)(hw1, hw2);
        }
    }

    return std::unexpected{CPUError::IllegalInstruction};
}

} // namespace micro_forge::cpu::arm::cortex_m3
