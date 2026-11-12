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

/// @brief C ABI bridge: rdi=cpu, rsi=vaddr, edx=width, ecx=mnemonic.
extern "C" void mf_jit_grp3_mem(void* cpu, uint64_t vaddr, unsigned width, unsigned mn) {
    static_cast<Cpu*>(cpu)->jit_grp3_mem(vaddr, width, mn);
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

/// @brief C ABI bridge: rdi=cpu, esi=slot, edx=index slot, ecx=width.
extern "C" void mf_jit_bt(void* cpu, unsigned slot, unsigned src, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_bt(slot, src, width);
}

} // namespace

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

void Cpu::jit_ports(unsigned mn, unsigned width) {
    // Port I/O only needs the mnemonic, the operand width and "port in dx":
    // execute_ports writes the device side effects, so sharing it keeps the
    // serial output byte-identical between the two engines.
    arch::x86_64::DecodedInsn shape{};
    shape.mnemonic = static_cast<Mnemonic>(mn);
    shape.opsize = static_cast<std::uint8_t>(width);
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

void Cpu::jit_bt(unsigned slot, unsigned src, unsigned width) {
    const auto value = read_gpr(static_cast<Reg>(slot / 8U), width, true);
    const auto index = read_gpr(static_cast<Reg>(src / 8U), width, true);
    const unsigned bits = width * 8U;
    cf_ = ((value >> (index & (bits - 1U))) & 1U) != 0U;
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
