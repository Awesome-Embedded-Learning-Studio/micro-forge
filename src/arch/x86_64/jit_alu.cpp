/// @file jit_alu.cpp
/// @brief ALU JIT helpers that reuse the interpreter's flag modelling.
#include <micro_forge/arch/x86_64/cpu.hpp>

#include "internal.hpp"

namespace micro_forge::arch::x86_64 {
namespace {

/// @brief C ABI bridge: rdi=cpu, esi=slot, rdx=count, ecx=mnemonic, r8d=width.
extern "C" void mf_jit_shift(void* cpu, unsigned slot, uint64_t count, unsigned mn,
                             unsigned width) {
    static_cast<Cpu*>(cpu)->jit_shift(slot, count, mn, width);
}

} // namespace

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
