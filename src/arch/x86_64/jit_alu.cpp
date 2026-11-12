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

} // namespace

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
