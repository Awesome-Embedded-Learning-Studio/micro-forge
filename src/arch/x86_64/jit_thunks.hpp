/**
 * @file jit_thunks.hpp
 * @brief C ABI bridges the copy-and-patch templates call into Cpu.
 *
 * Every helper takes the Cpu as its first argument (rdi from the template)
 * and lives in jit_cpu.cpp, jit_control.cpp or jit_alu.cpp. Keeping the
 * declarations in one place lets the define side reuse them and keeps
 * jit_cpu.cpp inside the file-line budget.
 */
#ifndef MICRO_FORGE_ARCH_X86_64_JIT_THUNKS_HPP
#define MICRO_FORGE_ARCH_X86_64_JIT_THUNKS_HPP

#include <cstddef>
#include <cstdint>

#include <micro_forge/base/jit_translate.hpp>

extern "C" std::uint64_t mf_jit_load(void* cpu, std::uint64_t vaddr, unsigned width);
extern "C" void mf_jit_store(void* cpu, std::uint64_t vaddr, unsigned width, std::uint64_t value);
extern "C" std::uint64_t mf_jit_add(void* cpu, std::uint64_t slot, std::uint64_t imm,
                                    unsigned width);
extern "C" void mf_jit_add_mem(void* cpu, std::uint64_t vaddr, std::uint64_t value);
extern "C" void mf_jit_cmp(void* cpu, uint64_t slot, uint64_t value, unsigned width);
extern "C" void mf_jit_test(void* cpu, uint64_t slot, uint64_t value, unsigned width);
extern "C" void mf_jit_sub(void* cpu, uint64_t slot, uint64_t value, unsigned width);
extern "C" void mf_jit_and(void* cpu, uint64_t slot, uint64_t value, unsigned width);
extern "C" void mf_jit_or(void* cpu, uint64_t slot, uint64_t value, unsigned width);
extern "C" void mf_jit_xor(void* cpu, uint64_t slot, uint64_t value, unsigned width);
extern "C" void mf_jit_imul(void* cpu, uint64_t slot, uint64_t value, unsigned width);
extern "C" void mf_jit_cmp_mem(void* cpu, uint64_t vaddr, uint64_t slot, unsigned width);
extern "C" void mf_jit_cmov(void* cpu, uint64_t slot, uint64_t src_val, unsigned cc,
                            unsigned width);
extern "C" void mf_jit_setcc(void* cpu, uint64_t slot, unsigned cc);
extern "C" void mf_jit_push_val(void* cpu, uint64_t value);
extern "C" void mf_jit_push_imm(void* cpu, uint64_t value);
extern "C" void mf_jit_load_xmm(void* cpu, uint64_t vaddr, uint64_t slot);
extern "C" void mf_jit_store_xmm(void* cpu, uint64_t vaddr, uint64_t slot);
extern "C" void mf_jit_ret(void* cpu, uint64_t sp, uint64_t my_rip);
extern "C" void mf_jit_leave(void* cpu);
extern "C" void mf_jit_jmp(void* cpu, uint64_t target);
extern "C" void mf_jit_jcc(void* cpu, uint64_t taken, uint64_t fall, unsigned cc);
extern "C" void mf_jit_mov_slot_w(void* cpu, uint64_t value, unsigned width, unsigned dst,
                                  unsigned zx);
extern "C" void mf_jit_shift(void* cpu, unsigned slot, uint64_t count, unsigned mn, unsigned width);
extern "C" void mf_jit_alu_reg(void* cpu, unsigned slot, unsigned src, unsigned mn, unsigned width);
extern "C" void mf_jit_alu_mem(void* cpu, unsigned slot, uint64_t vaddr, unsigned mn,
                               unsigned width);
extern "C" void mf_jit_pxor(void* cpu, unsigned dst, unsigned src);
extern "C" void mf_jit_grp3(void* cpu, uint64_t divisor, unsigned width, unsigned mn);
extern "C" void mf_jit_imul23(void* cpu, unsigned dst, unsigned src, uint64_t imm, unsigned width);
extern "C" void mf_jit_bt(void* cpu, unsigned slot, unsigned src, unsigned width);
extern "C" void mf_jit_cmov_mem(void* cpu, uint64_t vaddr, unsigned cc, unsigned slot,
                                unsigned width);
extern "C" void mf_jit_not(void* cpu, unsigned slot, unsigned unused, unsigned width);
extern "C" void mf_jit_alu_mem_imm(void* cpu, uint64_t vaddr, uint64_t imm, unsigned mn,
                                   unsigned width);
extern "C" void mf_jit_imul_mem(void* cpu, uint64_t vaddr, unsigned dst, unsigned width);
extern "C" void mf_jit_ports(void* cpu, unsigned mn, unsigned width);
extern "C" void mf_jit_alu_mem_idx(void* cpu, uint64_t vaddr, uint64_t src, unsigned mn,
                                   unsigned width);
extern "C" void mf_jit_sys_mem(void* cpu, uint64_t vaddr, unsigned width, unsigned mn);
extern "C" void mf_jit_grp3_mem(void* cpu, uint64_t vaddr, unsigned width, unsigned mn);
extern "C" void mf_jit_movsx(void* cpu, uint64_t value, unsigned src_width, unsigned dst,
                             unsigned dst_width);
extern "C" void mf_jit_neg(void* cpu, unsigned slot, unsigned unused, unsigned width);
extern "C" void mf_jit_bts(void* cpu, unsigned slot, unsigned index, unsigned width);
extern "C" void mf_jit_btr(void* cpu, unsigned slot, unsigned index, unsigned width);
extern "C" void mf_jit_btc(void* cpu, unsigned slot, unsigned index, unsigned width);

namespace micro_forge::arch::x86_64 {

/// @brief Returns the thunk address for a JitHelper id (jit_helpers.cpp).
/// @param[in] id Helper id recorded by the translated instruction.
/// @return Thunk address, or 0 when nothing matches.
std::uint64_t mf_jit_helper_address(micro_forge::base::JitHelper id);

} // namespace micro_forge::arch::x86_64

/// @brief Coverage diagnostics (jit_stats.cpp): count and dump fallbacks.
extern "C" void mf_jit_note_fail_hit(std::uint64_t rip);
extern "C" void mf_jit_dump_fail_hits();
/// @brief Coverage diagnostics: per-template translation hits
///        (MICRO_FORGE_JIT_TPLSTATS gates both).
extern "C" void mf_jit_note_template(const std::uint8_t* data);
extern "C" void mf_jit_dump_template_hits();
extern "C" void mf_jit_call(void* cpu, uint64_t target, uint64_t my_rip, uint64_t ret_addr);
extern "C" void mf_jit_pop_reg(void* cpu, uint64_t slot);

#endif // MICRO_FORGE_ARCH_X86_64_JIT_THUNKS_HPP
