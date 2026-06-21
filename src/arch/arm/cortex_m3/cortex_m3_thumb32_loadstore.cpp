#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "arch/arm/cortex_m3/thumb32_fields.hpp"

#include <bit>
#include <expected>

namespace micro_forge::cpu::arm::cortex_m3 {

using namespace thumb;

// ── Load/Store single data item (.W): str/ldr/strb/ldrb/strh/ldrh .W ──
// hw1[7] selects the immediate form:
//   1 → imm12 offset (T2/T3): addr = Rn + imm12, no writeback.
//   0 → imm8 with addressing modes, op = hw2[11:8]:
//       0=offset+, C=offset-, B=post+, 9=post-, F=pre+, D=pre-.
// Dispatched from execute_32bit when (hw1 & 0xFF00) == 0xF800.
CPU::CPUExpected<void> CortexM3CPU::t32_loadstore_single(uint16_t hw1,
                                                    uint16_t hw2) {
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

// ── TBB / TBH (Table Branch) ──
// Dispatched when (hw1 & 0xFFF0) == 0xE8D0 && (hw2 & 0xF0F0) == 0xF000.
// (TBH's H-bit handling here is a known T1 bug — see coverage matrix F32-9.)
CPU::CPUExpected<void> CortexM3CPU::t32_tbb_tbh(uint16_t hw1, uint16_t hw2) {
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
// Dispatched when (hw1 & 0xFE40) == 0xE840.
CPU::CPUExpected<void> CortexM3CPU::t32_strd_ldrd(uint16_t hw1, uint16_t hw2) {
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
// Dispatched when (hw1 & 0xFE40) == 0xE800.
CPU::CPUExpected<void> CortexM3CPU::t32_stm_ldm(uint16_t hw1, uint16_t hw2) {
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

} // namespace micro_forge::cpu::arm::cortex_m3
