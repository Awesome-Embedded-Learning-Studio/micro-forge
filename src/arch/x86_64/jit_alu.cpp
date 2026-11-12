/// @file jit_alu.cpp
/// @brief ALU JIT helpers that reuse the interpreter's flag modelling.
#include <micro_forge/arch/x86_64/cpu.hpp>

#include "internal.hpp"
#include "jit_thunks.hpp"

namespace micro_forge::arch::x86_64 {
namespace {

/// @brief C ABI bridge: rdi=cpu, esi=slot, rdx=count, ecx=mnemonic, r8d=width.
extern "C" void mf_jit_shift(void* cpu, unsigned slot, uint64_t count, unsigned mn,
                             unsigned width) {
    static_cast<Cpu*>(cpu)->jit_shift(slot, count, mn, width);
}

/// @brief C ABI bridge: rdi=cpu, esi=slot, edx=src, ecx=mnemonic, r8d=width.
extern "C" void mf_jit_alu_reg(void* cpu, unsigned slot, unsigned src, unsigned mn,
                               unsigned width) {
    static_cast<Cpu*>(cpu)->jit_alu_reg(slot, src, mn, width);
}

/// @brief C ABI bridge: rdi=cpu, esi=slot, rdx=vaddr, ecx=mnemonic, r8d=width.
extern "C" void mf_jit_alu_mem(void* cpu, unsigned slot, uint64_t vaddr, unsigned mn,
                               unsigned width) {
    static_cast<Cpu*>(cpu)->jit_alu_mem(slot, vaddr, mn, width);
}

/// @brief pxor helper: the public xmm accessors are enough, so this stays a
/// free function instead of another Cpu member.
extern "C" void mf_jit_pxor(void* cpu, unsigned dst, unsigned src) {
    auto* const c = static_cast<Cpu*>(cpu);
    const auto d = static_cast<Reg>(16U + (dst / 16U));
    const auto s = static_cast<Reg>(16U + (src / 16U));
    const auto a = c->xmm(d);
    const auto b = c->xmm(s);
    c->set_xmm(d, {a[0] ^ b[0], a[1] ^ b[1]});
}

/// @brief C ABI bridge: rdi=cpu, rsi=divisor, edx=width, ecx=mnemonic.
extern "C" void mf_jit_grp3(void* cpu, uint64_t divisor, unsigned width, unsigned mn) {
    static_cast<Cpu*>(cpu)->jit_grp3(divisor, width, mn);
}

/// @brief C ABI bridge: rdi=cpu, esi=dst, edx=src, rcx=imm, r8d=width.
extern "C" void mf_jit_imul23(void* cpu, unsigned dst, unsigned src, uint64_t imm, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_imul23(dst, src, imm, width);
}

/// @brief One thunk per bit operation; op selects the write-back flavour.
extern "C" void mf_jit_bts(void* cpu, unsigned slot, unsigned index, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_bit_op(slot, index, width, 1);
}

/// @brief btr thunk (op 2).
extern "C" void mf_jit_btr(void* cpu, unsigned slot, unsigned index, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_bit_op(slot, index, width, 2);
}

/// @brief btc thunk (op 3).
extern "C" void mf_jit_btc(void* cpu, unsigned slot, unsigned index, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_bit_op(slot, index, width, 3);
}

/// @brief C ABI bridge: rdi=cpu, esi=slot, edx=unused, ecx=width.
extern "C" void mf_jit_neg(void* cpu, unsigned slot, unsigned unused, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_neg(slot, unused, width);
}

/// @brief C ABI bridge: rdi=cpu, rsi=value, edx=src width, ecx=dst slot,
///        r8d=dst width.
extern "C" void mf_jit_movsx(void* cpu, uint64_t value, unsigned src_width, unsigned dst,
                             unsigned dst_width) {
    static_cast<Cpu*>(cpu)->jit_movsx(value, src_width, dst, dst_width);
}

/// @brief C ABI bridge: rdi=cpu, rsi=vaddr, edx=width, ecx=mnemonic.
extern "C" void mf_jit_grp3_mem(void* cpu, uint64_t vaddr, unsigned width, unsigned mn) {
    static_cast<Cpu*>(cpu)->jit_grp3_mem(vaddr, width, mn);
}

/// @brief C ABI bridge: rdi=cpu, rsi=vaddr, edx=width, ecx=mnemonic.
extern "C" void mf_jit_sys_mem(void* cpu, uint64_t vaddr, unsigned width, unsigned mn) {
    static_cast<Cpu*>(cpu)->jit_sys_mem(vaddr, width, mn);
}

/// @brief C ABI bridge: rdi=cpu, rsi=vaddr, rdx=src, ecx=mnemonic, r8d=width.
extern "C" void mf_jit_alu_mem_idx(void* cpu, uint64_t vaddr, uint64_t src, unsigned mn,
                                   unsigned width) {
    static_cast<Cpu*>(cpu)->jit_alu_mem_idx(vaddr, src, mn, width);
}

/// @brief C ABI bridge: rdi=cpu, esi=mnemonic, edx=width.
extern "C" void mf_jit_ports(void* cpu, unsigned mn, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_ports(mn, width);
}

/// @brief C ABI bridge: rdi=cpu, rsi=vaddr, edx=dst slot, ecx=width.
extern "C" void mf_jit_imul_mem(void* cpu, uint64_t vaddr, unsigned dst, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_imul_mem(vaddr, dst, width);
}

/// @brief C ABI bridge: rdi=cpu, rsi=vaddr, rdx=imm, ecx=mnemonic, r8d=width.
extern "C" void mf_jit_alu_mem_imm(void* cpu, uint64_t vaddr, uint64_t imm, unsigned mn,
                                   unsigned width) {
    static_cast<Cpu*>(cpu)->jit_alu_mem_imm(vaddr, imm, mn, width);
}

/// @brief not helper: only the public gpr accessors are needed, so this is a
/// free function rather than another Cpu member.
extern "C" void mf_jit_not(void* cpu, unsigned slot, unsigned unused, unsigned width) {
    static_cast<void>(unused); // shared three-argument template
    auto* const c = static_cast<Cpu*>(cpu);
    const auto reg = static_cast<Reg>(slot / 8U);
    const uint64_t value = c->gpr(reg);
    const uint64_t mask = width_util::width_mask(width);
    c->set_gpr(reg, (value & ~mask) | (~value & mask));
}

/// @brief C ABI bridge: rdi=cpu, rsi=vaddr, edx=cc, ecx=slot, r8d=width.
extern "C" void mf_jit_cmov_mem(void* cpu, uint64_t vaddr, unsigned cc, unsigned slot,
                                unsigned width) {
    static_cast<Cpu*>(cpu)->jit_cmov_mem(vaddr, cc, slot, width);
}

/// @brief C ABI bridge: rdi=cpu, esi=slot, edx=index, ecx=width. Plain bt is
/// jit_bit_op with op 0 (no write-back).
extern "C" void mf_jit_bt(void* cpu, unsigned slot, unsigned src, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_bit_op(slot, src, width, 0);
}

} // namespace

void Cpu::jit_bit_op(unsigned slot, unsigned index, unsigned width, unsigned op) {
    auto& dst = regs_[slot / 8U];
    const uint64_t mask = width_util::width_mask(width);
    const unsigned bits = width * 8U;
    const unsigned bit = index & (bits - 1U);
    const uint64_t value = dst & mask;
    cf_ = ((value >> bit) & 1U) != 0U;
    if (op == 0U) {
        return; // plain bt only touches CF
    }
    const uint64_t bitmask = 1ULL << bit;
    uint64_t updated = value;
    if (op == 1U) {
        updated |= bitmask;
    } else if (op == 2U) {
        updated &= ~bitmask;
    } else {
        updated ^= bitmask;
    }
    dst = (dst & ~mask) | (updated & mask);
}

void Cpu::jit_neg(unsigned slot, unsigned unused, unsigned width) {
    static_cast<void>(unused); // the shared three-argument template slot
    auto& dst = regs_[slot / 8U];
    const uint64_t mask = width_util::width_mask(width);
    const uint64_t result = alu_sub(0, dst & mask, width, false);
    dst = (dst & ~mask) | (result & mask);
}

void Cpu::jit_movsx(uint64_t value, unsigned src_width, unsigned dst, unsigned dst_width) {
    const uint64_t raw = value & width_util::width_mask(src_width);
    const auto extended = width_util::sign_extend(raw, src_width);
    // A 32-bit destination zero-extends the sign-extended value.
    regs_[dst / 8U] = dst_width == 4U ? static_cast<uint32_t>(extended) : extended;
}

void Cpu::jit_grp3_mem(uint64_t vaddr, unsigned width, unsigned mn) {
    uint64_t divisor = 0;
    if (!load(vaddr, width, divisor)) {
        return; // faulted: the interpreter re-runs this instruction
    }
    arch::x86_64::DecodedInsn shape{};
    shape.mnemonic = static_cast<Mnemonic>(mn);
    shape.opsize = static_cast<std::uint8_t>(width);
    (void)execute_grp3(shape, divisor);
}

void Cpu::jit_sys_mem(uint64_t vaddr, unsigned width, unsigned mn) {
    // execute_system reaches the operand through read_operand, so the shape
    // carries a rip-relative memory operand whose disp is the address we
    // already computed (rip_after 0 makes effective_address return it as is).
    arch::x86_64::DecodedInsn shape{};
    shape.mnemonic = static_cast<Mnemonic>(mn);
    shape.opsize = static_cast<std::uint8_t>(width);
    shape.ops[0] = {.kind = Operand::Kind::kMem, .size = static_cast<std::uint8_t>(width)};
    shape.ops[0].rip_relative = true;
    shape.ops[0].disp = static_cast<std::int64_t>(vaddr);
    (void)execute_system(shape, 0);
}

void Cpu::jit_alu_mem_idx(uint64_t vaddr, uint64_t src, unsigned mn, unsigned width) {
    uint64_t mem = 0;
    if (!load(vaddr, width, mem)) {
        return; // faulted: the interpreter re-runs this instruction
    }
    // test takes a register slot, cmp an immediate; neither writes memory.
    const auto kind = static_cast<Mnemonic>(mn);
    const uint64_t other =
        kind == Mnemonic::kTest ? read_gpr(static_cast<Reg>(src / 8U), width, true) : src;
    if (kind == Mnemonic::kTest) {
        (void)alu_logic(mem & other, width);
    } else {
        (void)alu_sub(mem, other, width, false);
    }
}

void Cpu::jit_ports(unsigned mn, unsigned width) {
    // Port I/O only needs the mnemonic, the operand width and "port in dx":
    // execute_ports writes the device side effects, so sharing it keeps the
    // serial output byte-identical between the two engines.
    arch::x86_64::DecodedInsn shape{};
    shape.mnemonic = static_cast<Mnemonic>(mn);
    shape.opsize = static_cast<std::uint8_t>(width);
    // Operand-less system instructions all live in execute_system; the only
    // shape they need is the mnemonic (they read fixed registers themselves).
    switch (shape.mnemonic) {
    case Mnemonic::kCli:
    case Mnemonic::kSti:
    case Mnemonic::kClac:
    case Mnemonic::kStac:
    case Mnemonic::kFninit:
    case Mnemonic::kRdmsr:
    case Mnemonic::kWrmsr:
    case Mnemonic::kRdtsc:
    case Mnemonic::kCpuid:
    case Mnemonic::kRdrand:
    case Mnemonic::kPushfq:
    case Mnemonic::kPopfq:
        (void)execute_system(shape, 0); // iret/lgdt need control flow or memory
        return;
    default:
        break;
    }
    const auto w8 = static_cast<std::uint8_t>(width);
    const auto dx = Operand{.kind = Operand::Kind::kImplicit, .size = 2, .reg = Reg::kRdx};
    const auto acc = Operand{.kind = Operand::Kind::kImplicit, .size = w8, .reg = Reg::kRax};
    if (shape.mnemonic == Mnemonic::kIn) {
        shape.ops[0] = acc;
        shape.ops[1] = dx;
    } else {
        shape.ops[0] = dx;
        shape.ops[1] = acc;
    }
    (void)execute_ports(shape, 0);
}

void Cpu::jit_imul_mem(uint64_t vaddr, unsigned dst, unsigned width) {
    uint64_t multiplier = 0;
    if (!load(vaddr, width, multiplier)) {
        return; // faulted: the interpreter re-runs this instruction
    }
    // Two-operand imul is dst = dst * src; the three-operand shape with
    // ops[1] == ops[0] and the loaded value as the immediate says exactly
    // that, so execute_imul23 keeps the flag rule.
    arch::x86_64::DecodedInsn shape{};
    shape.opsize = static_cast<std::uint8_t>(width);
    const auto w8 = static_cast<std::uint8_t>(width);
    const auto reg = static_cast<Reg>(dst / 8U);
    const auto mval = static_cast<std::int64_t>(multiplier);
    shape.ops[0] = {.kind = Operand::Kind::kReg, .size = w8, .reg = reg};
    shape.ops[1] = {.kind = Operand::Kind::kReg, .size = w8, .reg = reg};
    shape.ops[2] = {.kind = Operand::Kind::kImm, .size = w8, .disp = mval};
    (void)execute_imul23(shape, 0);
}

void Cpu::jit_alu_mem_imm(uint64_t vaddr, uint64_t imm, unsigned mn, unsigned width) {
    uint64_t value = 0;
    if (!load(vaddr, width, value)) {
        return; // faulted: the interpreter re-runs this instruction
    }
    uint64_t result = 0;
    bool writes = true;
    switch (static_cast<Mnemonic>(mn)) {
    case Mnemonic::kAdd:
        result = alu_add(value, imm, width, false);
        break;
    case Mnemonic::kSub:
        result = alu_sub(value, imm, width, false);
        break;
    case Mnemonic::kCmp:
        result = alu_sub(value, imm, width, false);
        writes = false; // flags only, the memory operand stays as it was
        break;
    case Mnemonic::kAnd:
        result = alu_logic(value & imm, width);
        break;
    case Mnemonic::kOr:
        result = alu_logic(value | imm, width);
        break;
    default: // kXor
        result = alu_logic(value ^ imm, width);
        break;
    }
    if (writes) {
        (void)store(vaddr, width, result);
    }
}

void Cpu::jit_cmov_mem(uint64_t vaddr, unsigned cc, unsigned slot, unsigned width) {
    // x86 cmov reads the source unconditionally (a faulting read still
    // raises #PF); only the write is conditional on the flags.
    uint64_t value = 0;
    if (!load(vaddr, width, value)) {
        return; // faulted: the interpreter re-runs this instruction
    }
    if (!condition(static_cast<Cc>(cc))) {
        return;
    }
    // write_gpr has the same width rules as the slot writes: 32-bit
    // zero-extends, narrower widths keep the high bytes.
    write_gpr(static_cast<Reg>(slot / 8U), width, true, value & width_util::width_mask(width));
}

void Cpu::jit_imul23(unsigned dst, unsigned src, uint64_t imm, unsigned width) {
    // execute_imul23 reads the two source operands and their sizes; the
    // destination is written through write_operand, so the shape carries
    // real register ids picked from the slot offsets.
    arch::x86_64::DecodedInsn shape{};
    shape.opsize = static_cast<std::uint8_t>(width);
    const auto w8 = static_cast<std::uint8_t>(width);
    const auto dreg = static_cast<Reg>(dst / 8U);
    const auto sreg = static_cast<Reg>(src / 8U);
    const auto sdisp = static_cast<std::int64_t>(imm);
    shape.ops[0] = {.kind = Operand::Kind::kReg, .size = w8, .reg = dreg};
    shape.ops[1] = {.kind = Operand::Kind::kReg, .size = w8, .reg = sreg};
    shape.ops[2] = {.kind = Operand::Kind::kImm, .size = w8, .disp = sdisp};
    (void)execute_imul23(shape, 0);
}

void Cpu::jit_grp3(uint64_t divisor, unsigned width, unsigned mn) {
    // execute_grp3 only reads the mnemonic and the operand size, so a
    // zero-initialized shape is enough; flags and rdx:rax are handled there.
    arch::x86_64::DecodedInsn shape{};
    shape.mnemonic = static_cast<Mnemonic>(mn);
    shape.opsize = static_cast<std::uint8_t>(width);
    (void)execute_grp3(shape, divisor);
}

void Cpu::jit_alu_mem(unsigned slot, uint64_t vaddr, unsigned mn, unsigned width) {
    uint64_t vb = 0;
    if (!load(vaddr, width, vb)) {
        return; // faulted: the interpreter re-runs this instruction
    }
    const auto dst = static_cast<Reg>(slot / 8U);
    const uint64_t va = read_gpr(dst, width, true);
    uint64_t result = 0;
    bool writes = true;
    // Keep in step with jit_alu_reg: same mnemonics, same flag helpers.
    switch (static_cast<Mnemonic>(mn)) {
    case Mnemonic::kAdd:
        result = alu_add(va, vb, width, false);
        break;
    case Mnemonic::kAdc:
        result = alu_add(va, vb, width, cf_);
        break;
    case Mnemonic::kSub:
        result = alu_sub(va, vb, width, false);
        break;
    case Mnemonic::kSbb:
        result = alu_sub(va, vb, width, cf_);
        break;
    case Mnemonic::kAnd:
        result = alu_logic(va & vb, width);
        break;
    case Mnemonic::kOr:
        result = alu_logic(va | vb, width);
        break;
    case Mnemonic::kXor:
        result = alu_logic(va ^ vb, width);
        break;
    default: // kCmp: flags only, no write-back
        result = alu_sub(va, vb, width, false);
        writes = false;
        break;
    }
    if (writes) {
        write_gpr(dst, width, true, result);
    }
}

void Cpu::jit_alu_reg(unsigned slot, unsigned src, unsigned mn, unsigned width) {
    const auto dst = static_cast<Reg>(slot / 8U);
    const auto sop = static_cast<Reg>(src / 8U);
    const uint64_t va = read_gpr(dst, width, true);
    const uint64_t vb = read_gpr(sop, width, true);
    uint64_t result = 0;
    bool writes = true;
    switch (static_cast<Mnemonic>(mn)) {
    case Mnemonic::kAdd:
        result = alu_add(va, vb, width, false);
        break;
    case Mnemonic::kAdc:
        result = alu_add(va, vb, width, cf_);
        break;
    case Mnemonic::kSub:
        result = alu_sub(va, vb, width, false);
        break;
    case Mnemonic::kSbb:
        result = alu_sub(va, vb, width, cf_);
        break;
    case Mnemonic::kAnd:
        result = alu_logic(va & vb, width);
        break;
    case Mnemonic::kOr:
        result = alu_logic(va | vb, width);
        break;
    case Mnemonic::kXor:
        result = alu_logic(va ^ vb, width);
        break;
    default: // kCmp: flags only, no write-back
        result = alu_sub(va, vb, width, false);
        writes = false;
        break;
    }
    if (writes) {
        write_gpr(dst, width, true, result);
    }
}

void Cpu::jit_shift(unsigned slot, uint64_t count, unsigned mn, unsigned width) {
    uint64_t& value = regs_[slot / 8U];
    // Count 0 leaves every flag untouched and skips the write entirely,
    // exactly like the interpreter's eff==0 guard.
    if ((count & width_util::shift_count_mask(width)) == 0U) {
        return;
    }
    (void)alu_shift(static_cast<Mnemonic>(mn), value, width, count);
}

} // namespace micro_forge::arch::x86_64
