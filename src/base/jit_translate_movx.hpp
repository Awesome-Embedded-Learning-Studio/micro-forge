/**
 * @file jit_translate_movx.hpp
 * @brief Internal split of jit_translate.cpp: SSE movups-family lowering.
 */
#ifndef MICRO_FORGE_BASE_JIT_TRANSLATE_MOVX_HPP
#define MICRO_FORGE_BASE_JIT_TRANSLATE_MOVX_HPP

#include <micro_forge/base/jit_translate.hpp>

#include <cstdint>
#include <optional>

namespace micro_forge::base::detail {

/**
 * @brief Lowers the movups/movaps/movdqa/movdqu forms: 16-byte moves
 *        between an xmm register and memory, base and indexed.
 * @param insn decoded guest instruction.
 * @return Translated form, or nullopt when the shape is not handled.
 */
std::optional<TranslatedInsn> translate_movx(const arch::x86_64::DecodedInsn& insn);

} // namespace micro_forge::base::detail

#endif // MICRO_FORGE_BASE_JIT_TRANSLATE_MOVX_HPP
