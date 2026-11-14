/**
 * @file wasm_block.hpp
 * @brief Guest basic blocks lowered to WebAssembly function bodies.
 * @version 0.2.0
 *
 * Second JIT backend: every guest register is an i64 slot in linear
 * memory (slot = reg * 8), so a guest instruction becomes a short wasm
 * sequence. The page compiles the emitted module with WebAssembly.compile
 * and calls the exported function once per block run.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "micro_forge/arch/x86_64/decoded.hpp"

namespace micro_forge::base {

/// @brief How a lowered block ends.
enum class WasmTerm : std::uint8_t {
    kNext,   ///< Ran out of instructions: returns the next sequential rip.
    kStep,   ///< Ends on ret: returns the all-ones sentinel, caller steps.
    kJump,   ///< Ends on jmp imm: returns the constant target rip.
    kBranch, ///< Ends on jcc: returns taken or fallthrough rip by flags.
};

/// @brief Liveness and exit facts the JS run loop bridges with.
struct WasmBlockInfo {
    std::size_t lowered = 0;         ///< Instructions lowered into the body.
    std::uint32_t used_mask = 0;     ///< GPR slots loaded from memory on entry.
    std::uint32_t dirty_mask = 0;    ///< GPR slots stored back to memory on exit.
    bool flags_used = false;         ///< Body reads the flags cell.
    bool flags_dirty = false;        ///< Body writes the flags cell.
    WasmTerm term = WasmTerm::kNext; ///< Exit shape of the block.
    std::uint64_t jump_target = 0;   ///< kJump / kBranch taken rip.
    std::uint64_t fall_rip = 0;      ///< kBranch fallthrough rip.
    std::uint32_t stopped_at = 0;    ///< Mnemonic code that stopped the slice.
};

/**
 * @brief Lowers a whole block into the register-bridged body.
 *
 * Sixteen i64 locals bridge the guest GPRs: used slots load on entry,
 * dirtied slots store back on exit, and the body ends by returning the
 * next guest rip. Blocks end at the first unsupported instruction, a ret
 * (all-ones sentinel; the stack read stays with the interpreter step),
 * a jmp imm (constant target) or a jcc (select between taken and
 * fallthrough over the flags cell).
 *
 * @param[in] insns Decoded instructions in execution order.
 * @param[in] base_rip Guest rip of the first instruction.
 * @param[in,out] body Wasm body bytes (locals vector excluded).
 * @param[in] gpr_base Static memory offset of the GPR mirror.
 * @param[in] ram_limit Exclusive guest-physical RAM bound for memory
 *            operands (the inline page walk refuses beyond it).
 * @return Lowered count, liveness masks, and the exit shape.
 */
[[nodiscard]] WasmBlockInfo append_wasm_block(std::span<const arch::x86_64::DecodedInsn> insns,
                                              uint64_t base_rip, std::vector<std::uint8_t>& body,
                                              std::uint32_t gpr_base = 0,
                                              std::uint32_t ram_limit = 0x10000000U);

/// @brief Byte offset of the packed i32 flags cell in the block memory.
/// @return cf|zf<<1|sf<<2|of<<3|pf<<4|af<<5 lives at this offset.
[[nodiscard]] std::size_t wasm_block_flags_slot();

} // namespace micro_forge::base
