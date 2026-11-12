/**
 * @file jit_translate_alu.hpp
 * @brief Internal split of jit_translate.cpp: ALU-family lowering only.
 */

#ifndef MICRO_FORGE_BASE_JIT_TRANSLATE_ALU_HPP
#define MICRO_FORGE_BASE_JIT_TRANSLATE_ALU_HPP

#include <micro_forge/base/jit_translate.hpp>

#include <cstdint>
#include <optional>

namespace micro_forge::base::detail {

/**
 * @brief Lowers the shift-by-cl and pxor forms, which need their own
 *        templates but no generic ALU helper.
 * @param insn decoded guest instruction.
 * @param rip Instruction address, needed by the rip-relative lea form.
 * @return Translated form, or nullopt when the shape is not handled.
 */
std::optional<TranslatedInsn> translate_alu_extra(const arch::x86_64::DecodedInsn& insn,
                                                  uint64_t rip = 0);

/**
 * @brief Maps an ALU mnemonic to its JIT helper id.
 * @param mnemonic decoded guest mnemonic.
 * @return Helper id, or kNone when the mnemonic is not an ALU op.
 */
[[nodiscard]] JitHelper alu_helper(arch::x86_64::Mnemonic mnemonic);

/**
 * @brief Lowers the ALU forms: reg<-imm, reg<-reg, and cmp reg,[mem].
 *
 * Width gate: cmp/test/sub/and/or/xor accept 1/2/4/8 bytes; imul only
 * the 4/8 two-operand forms (its flags come from mul_wide primitives).
 *
 * @param insn decoded guest instruction (ops[0] already a GPR).
 * @param slot byte offset of ops[0] inside the register file.
 * @param helper helper id from alu_helper().
 * @return Lowered insn, or nullopt when no template fits.
 */
[[nodiscard]] std::optional<TranslatedInsn> translate_alu(const arch::x86_64::DecodedInsn& insn,
                                                          std::size_t slot, JitHelper helper);

} // namespace micro_forge::base::detail

#endif // MICRO_FORGE_BASE_JIT_TRANSLATE_ALU_HPP
