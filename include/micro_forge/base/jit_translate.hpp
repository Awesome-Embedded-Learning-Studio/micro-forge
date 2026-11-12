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
    kImul,      ///< two/three-operand imul: reg = reg * r/m, CF/OF from truncation
    kCmov,      ///< cmovcc reg,reg: dst = src when the condition holds
    kSetcc,     ///< setcc reg8: dst byte = 1 when the condition holds
    kPushVal,   ///< push reg64 value: stack write via rsp
    kLoadXmm,   ///< movups-family load: [base+disp] -> xmm
    kStoreXmm,  ///< movups-family store: xmm -> [base+disp]
    kRet,       ///< ret C3: pop rip into the block exit target
    kCall,      ///< call: push ret addr, jump via the block exit target
    kJmp,       ///< jmp imm: record the exit target
    kJcc,       ///< jcc: pick taken/fallthrough by guest flags
    kMovSlotW,  ///< movzx / narrow mov reg,reg via one width helper
    kShift,     ///< shift reg,imm8 via the interpreter's alu_shift
    kAluReg,    ///< add/adc/sbb reg,reg via alu_add/alu_sub
    kAluMem,    ///< ALU reg,[base+disp] via the same alu helpers
    kShiftCl,   ///< shift reg,cl: same helper, count read at run time
    kPxor,      ///< pxor xmm,xmm through a 16-byte helper
    kAluMemRip, ///< ALU reg,[rip+disp]: constant address, same helper
    kLoadRip,   ///< movzx reg,[rip+disp]: constant address load
    kGrp3,      ///< one-operand mul/imul/div/idiv via execute_grp3
    kImul23,    ///< three-operand imul via execute_imul23
    kBt,        ///< bt: CF = tested bit; no other flag changes
    kCmovMem,   ///< cmov reg,[base+disp]: load, then merge if cc holds
    kNot,       ///< not reg: complement, flags untouched
    kAluMemImm, ///< ALU [base+disp],imm: load, combine, store back
    kImulMem,   ///< imul reg,[base+disp]: load, then the imul23 shape
    kLeave,     ///< leave: rsp=rbp then pop rbp
    kPushImm,   ///< push imm64 (pre-extended)
    kPopReg,    ///< pop reg64: stack read via rsp
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
