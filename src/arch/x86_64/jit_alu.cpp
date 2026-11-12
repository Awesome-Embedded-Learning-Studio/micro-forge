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

} // namespace

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
