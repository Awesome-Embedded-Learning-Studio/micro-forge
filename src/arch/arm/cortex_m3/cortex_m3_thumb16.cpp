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

// ── Inline cases lifted out of execute_16bit (Step 1 of JIT translate:
// dispatch becomes a pure handler binding). Bodies are byte-identical to the
// former inline blocks — only moved into named functions. ──

CPU::CPUExpected<void> CortexM3CPU::t16_cps(uint16_t insn, uint16_t) { // CPSIE/CPSID
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

CPU::CPUExpected<void> CortexM3CPU::t16_cbz(uint16_t insn, uint16_t) { // CBZ/CBNZ
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

CPU::CPUExpected<void> CortexM3CPU::t16_adr(uint16_t insn, uint16_t) { // ADR (PC-relative)
    uint8_t rd = rd8(insn);
    auto pc_res = read_pc_raw();
    if (!pc_res) {
        return std::unexpected{pc_res.error()};
    }
    uint32_t base = (*pc_res + 4u) & ~3u;
    return wr(rd, base + imm8(insn) * 4u);
}

CPU::CPUExpected<void> CortexM3CPU::t16_add_sp(uint16_t insn, uint16_t) { // ADD Rd, SP
    return wr(rd8(insn), rr(13) + imm8(insn) * 4u);
}

CPU::CPUExpected<void> CortexM3CPU::t16_b_cond(uint16_t insn, uint16_t) { // B<cond>
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
    return {};
}

CPU::CPUExpected<void> CortexM3CPU::t16_b(uint16_t insn, uint16_t) { // B (unconditional)
    int32_t offset = static_cast<int16_t>(imm11(insn) << 5) >> 5;
    offset <<= 1;
    auto pc_res = read_pc_raw();
    if (!pc_res) {
        return std::unexpected{pc_res.error()};
    }
    return write_reg(15, *pc_res + 4 + offset);
}

CortexM3CPU::Handler16
CortexM3CPU::translate_16bit(uint16_t insn) const noexcept {
    // Same dispatch as execute_16bit, but returns the handler instead of
    // calling it. nullptr = illegal opcode. A translation cache binds
    // {PC → handler} once and skips fetch+decode on hit (JIT). Prefix-probe
    // order is load-bearing (CPS/CBZ/extend/reverse before decode_key).
    if ((insn & 0xFFE0u) == 0xB660u) return &CortexM3CPU::t16_cps;
    if ((insn & 0xF500u) == 0xB100u) return &CortexM3CPU::t16_cbz;
    if ((insn & 0xFF00u) == 0xB200u) return &CortexM3CPU::t16_extend;
    if ((insn & 0xFF00u) == 0xBA00u) return &CortexM3CPU::t16_reverse;
    switch (decode_key(insn)) {
        case 0b00000: case 0b00001: case 0b00010:
            return &CortexM3CPU::t16_shift_imm;
        case 0b00011:
            return &CortexM3CPU::t16_addsub_reg3;
        case 0b00100: case 0b00101: case 0b00110: case 0b00111:
            return &CortexM3CPU::t16_imm8_dataops;
        case 0b01000:
            return ((insn >> 10) & 1) ? &CortexM3CPU::t16_special_bx
                                      : &CortexM3CPU::t16_dataproc_reg;
        case 0b01001: return &CortexM3CPU::t16_ldr_literal;
        case 0b01010: case 0b01011:
            return &CortexM3CPU::t16_loadstore_reg_offset;
        case 0b01100: case 0b01101: case 0b01110: case 0b01111:
        case 0b10000: case 0b10001:
            return &CortexM3CPU::t16_loadstore_imm_offset;
        case 0b10010: case 0b10011:
            return &CortexM3CPU::t16_loadstore_sp_rel;
        case 0b10100: return &CortexM3CPU::t16_adr;
        case 0b10101: return &CortexM3CPU::t16_add_sp;
        case 0b10110: return &CortexM3CPU::t16_push;
        case 0b10111: return &CortexM3CPU::t16_pop;
        case 0b11000: case 0b11001: return &CortexM3CPU::t16_stm_ldm;
        case 0b11010: case 0b11011: return &CortexM3CPU::t16_b_cond;
        case 0b11100: return &CortexM3CPU::t16_b;
        default: return nullptr;
    }
}

CPU::CPUExpected<void> CortexM3CPU::execute_16bit(uint16_t insn) {
    // Dispatch is now in translate_16bit (shared with the JIT cache). Look up
    // the handler, then execute it — bit-identical to the former inline
    // switch, just routed through a function pointer.
    const Handler16 h = translate_16bit(insn);
    if (h == nullptr) {
        return std::unexpected{CPUError::IllegalInstruction};
    }
    return (this->*h)(insn, 0);
}

} // namespace micro_forge::cpu::arm::cortex_m3
