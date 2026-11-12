/**
 * @file jit_translate_mov.hpp
 * @brief Internal split of jit_translate.cpp: mov-family lowering only.
 */

#ifndef MICRO_FORGE_BASE_JIT_TRANSLATE_MOV_HPP
#define MICRO_FORGE_BASE_JIT_TRANSLATE_MOV_HPP

#include <micro_forge/base/jit_templates_generated.hpp>
#include <micro_forge/base/jit_translate.hpp>

#include <cstddef>
#include <optional>
#include <span>

namespace micro_forge::base::detail {

namespace jg = micro_forge::base::jit_gen;

// Every entry here is `inline`, so each TU gets one definition and the
// anonymous namespace only keeps the names local; the lint's concern (ODR
// trouble) does not apply. Kept here rather than in the .cpp because that
// file is at the 600-line limit.
// NOLINTNEXTLINE(misc-anonymous-namespace-in-header)
namespace {

/// @brief Template bytes for a mov-load at the given write-back width.
/// @param[in] width Guest operand width in bytes (1, 2, 4 or 8).
/// @return Assembled template; 4- and 8-byte forms share one.
inline std::span<const std::uint8_t> load_bytes(unsigned width) {
    if (width == 1) {
        return jg::kLoadSlotB;
    }
    if (width == 2) {
        return jg::kLoadSlotW;
    }
    return jg::kLoadSlot;
}

/// @brief Hole table matching load_bytes().
/// @param[in] width Guest operand width in bytes.
/// @return Hole table for the chosen template.
inline std::span<const jg::Hole> load_holes(unsigned width) {
    if (width == 1) {
        return jg::kLoadSlotBHoles;
    }
    if (width == 2) {
        return jg::kLoadSlotWHoles;
    }
    return jg::kLoadSlotHoles;
}

/// @brief Template bytes for an indexed mov-load at the given width.
/// @param[in] width Guest operand width in bytes.
/// @return Assembled template.
inline std::span<const std::uint8_t> load_idx_bytes(unsigned width) {
    if (width == 1) {
        return jg::kLoadIdxSlotB;
    }
    if (width == 2) {
        return jg::kLoadIdxSlotW;
    }
    return jg::kLoadIdxSlot;
}

/// @brief Hole table matching load_idx_bytes().
/// @param[in] width Guest operand width in bytes.
/// @return Hole table for the chosen template.
inline std::span<const jg::Hole> load_idx_holes(unsigned width) {
    if (width == 1) {
        return jg::kLoadIdxSlotBHoles;
    }
    if (width == 2) {
        return jg::kLoadIdxSlotWHoles;
    }
    return jg::kLoadIdxSlotHoles;
}

} // namespace

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
