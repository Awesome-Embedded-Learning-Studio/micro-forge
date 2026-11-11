/**
 * @file jit_translate.hpp
 * @brief Guest instruction to copy-and-patch template lowering.
 * @version 0.1.0
 *
 * The first slice of the JIT lowering: a decoded instruction becomes a
 * fixed machine-code template plus the holes that carry its register
 * slot and immediate. Unsupported instructions return nullopt, which
 * ends the block and leaves those instructions to the interpreter.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "micro_forge/arch/x86_64/decoded.hpp"
#include "micro_forge/base/jit.hpp"

namespace micro_forge::base {

/// @brief Which C++ helper a template calls (kNone when it calls none).
enum class JitHelper : std::uint8_t {
    kNone,
    kLoad,
    kStore,
    kAdd,
    kAddMem,
    kCmp,
    kTest,
    kSub,
    kAnd,
    kOr,
    kXor,
    kCmpMem,
    kImul, ///< two/three-operand imul: reg = reg * r/m, CF/OF from truncation
};

/**
 * @brief One lowered instruction: template bytes, holes, and helper info.
 */
struct TranslatedInsn {
    std::span<const std::uint8_t> code;  ///< Template machine code.
    std::vector<JitPage::Patch> patches; ///< Holes to fill when emitting.
    /// Byte offset of the imm64 hole in movabs r11,helper (nullopt when none);
    /// the compiler fills the complete helper address before call r11.
    std::optional<std::size_t> call_offset;
    /// Byte offset of the Cpu* argument hole (nullopt when none).
    std::optional<std::size_t> cpu_offset;
    /// Helper the template calls (kNone for pure templates).
    JitHelper helper = JitHelper::kNone;

    /// Checks relocation opcodes, zero-filled holes, bounds and overlaps
    /// before any patch can overwrite the template's instruction bytes.
    [[nodiscard]] bool has_valid_layout() const;
};

/**
 * @brief Lowers one decoded instruction to a template.
 *
 * Supported today: mov imm32 and add reg,imm32 (32/64 bits), 64-bit register
 * mov, lea base+disp and RIP-relative load; base+index*scale+disp loads/stores
 * support 1/2/4/8 bytes, preserving partial registers for byte/word loads.
 * Also supports add [base+disp],reg.
 * Memory add supports unsegmented, unlocked qwords only.
 * cmp/test/sub/and/or/xor support 32/64-bit reg,imm and reg,reg;
 * cmp also accepts an unsegmented, unlocked base+index*scale+disp source.
 * Everything else returns nullopt and stays with the interpreter.
 *
 * @param[in] insn The decoded instruction.
 * @return The template and its holes, or nullopt when unsupported.
 */
[[nodiscard]] std::optional<TranslatedInsn> translate_insn(const arch::x86_64::DecodedInsn& insn,
                                                           uint64_t rip = 0);

/**
 * @brief Guest register slot offset inside the JIT register file.
 *
 * @param[in] reg The guest register.
 * @return Byte offset (reg * 8), or nullopt for non-GPR registers.
 */
[[nodiscard]] std::optional<std::size_t> reg_slot_offset(arch::x86_64::Reg reg);

} // namespace micro_forge::base
