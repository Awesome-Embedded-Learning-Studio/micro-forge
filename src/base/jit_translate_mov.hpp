/**
 * @file jit_translate_mov.hpp
 * @brief Internal split of jit_translate.cpp: mov-family lowering only.
 */

#ifndef MICRO_FORGE_BASE_JIT_TRANSLATE_MOV_HPP
#define MICRO_FORGE_BASE_JIT_TRANSLATE_MOV_HPP

#include <micro_forge/base/jit_translate.hpp>

#include <cstddef>
#include <optional>

namespace micro_forge::base::detail {

/**
 * @brief Maps an operand to its byte offset inside the register file.
 * @param op decoded operand (kReg only).
 * @param rex whether a REX prefix was present.
 * @return Slot offset; AH/CH/DH/BH fold to byte 1 of the parent slot.
 */
[[nodiscard]] std::optional<std::size_t> operand_slot(const arch::x86_64::Operand& op, bool rex);

/**
 * @brief Checks whether @p width is a byte-valid memory width.
 * @param width operand width in bytes.
 * @return True for 1/2/4/8.
 */
[[nodiscard]] bool memory_width(unsigned width);

/**
 * @brief Lowers the mov forms: store reg/imm, load reg (indexed and
 * rip-relative included).
 *
 * @param insn decoded guest instruction.
 * @param rip guest rip of the instruction.
 * @return Lowered insn, or nullopt when no template fits.
 */
[[nodiscard]] std::optional<TranslatedInsn> translate_move(const arch::x86_64::DecodedInsn& insn,
                                                           std::uint64_t rip);

} // namespace micro_forge::base::detail

#endif // MICRO_FORGE_BASE_JIT_TRANSLATE_MOV_HPP
