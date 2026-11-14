/**
 * @file wasm_block_internal.hpp
 * @brief Shared plumbing between the wasm-block lowering translation units.
 * @version 0.1.0
 *
 * Handoff facts, the early-return emitters and the page-table walk entry
 * point are shared across wasm_block_mem.cpp and wasm_block_walk.cpp.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "micro_forge/arch/x86_64/decoded.hpp"
#include "micro_forge/base/wasm_block.hpp"

namespace micro_forge::base::detail {

/// Exit shape one terminating instruction reports to the block builder.
struct TermExit {
    WasmTerm kind = WasmTerm::kNext; ///< how the block ends.
    std::uint64_t taken = 0;         ///< jump/branch target.
    std::uint64_t fall = 0;          ///< branch fallthrough rip.
};

/// Facts a mid-block refusal needs to hand the rest back to the runner.
struct Handoff {
    std::uint32_t base;       ///< GPR mirror base offset.
    unsigned done;            ///< instructions of this block already complete.
    std::uint64_t rip;        ///< guest rip the interpreter resumes from.
    std::uint32_t dirty_bits; ///< locals written so far that must reach the mirror.
};

/// @brief Marks the mirror handed flag and returns @p ho.rip.
/// Records done/dirty so the runner commits what already ran.
/// @param[in] reason Nonzero tag recorded in kHandoffReasonSlot for
///            diagnostics (1 = walk out of RAM, 2 = page absent,
///            3 = paddr out of RAM, 4 = user access to a supervisor page,
///            5 = write to a read-only page).
void emit_handoff(std::vector<std::uint8_t>& body, const Handoff& ho, unsigned reason = 0);

/// @brief Emits the effective address of @p op into kTmpV (no guest access).
/// @param[in,out] body Opcode stream under construction.
/// @param[in] op Memory operand (base + index*scale + disp).
/// @return false when the operand uses a form the block cannot address
///         (rip-relative, or a register outside the bridged set).
bool emit_vaddr(const micro_forge::arch::x86_64::Operand& op, std::vector<std::uint8_t>& body);

/// @brief Emits a guest load of @p size bytes (address on stack, value left).
/// @param[in,out] body Opcode stream under construction.
/// @param[in] size Access width in bytes (1/2/4/8).
void emit_guest_load(std::vector<std::uint8_t>& body, unsigned size);

/// @brief Emits a guest store of @p size bytes (address then value on stack).
/// @param[in,out] body Opcode stream under construction.
/// @param[in] size Access width in bytes (1/2/4/8).
void emit_guest_store(std::vector<std::uint8_t>& body, unsigned size);

/// @brief Records the paddr in kTmpPd in the store log (SMC detection).
/// @param[in,out] body Opcode stream under construction.
/// @param[in] base GPR mirror base offset.
void emit_store_log(std::vector<std::uint8_t>& body, std::uint32_t base);

/// @brief Lowers a guest stack operation into @p body.
///
/// Handles `push r`, `pop r` and `ret`: all three move rsp through the same
/// page walk the data instructions use, so a refusal leaves the stack
/// untouched and hands the instruction back to the interpreter.
/// @param[in] insn Decoded instruction (stack forms only).
/// @param[in] rip Guest rip of the instruction.
/// @param[in,out] body Opcode stream under construction.
/// @param[in,out] term Exit shape (ret reports kIndirect).
/// @param[in] base GPR mirror base offset.
/// @param[in] ram_limit First physical address outside guest RAM.
/// @param[in] done Instructions already complete in this block.
/// @param[in] dirty_so_far Mirror commit mask so far.
/// @return true when the form was lowered; false keeps it with the interpreter.
bool lower_stack_insn(const micro_forge::arch::x86_64::DecodedInsn& insn, std::uint64_t rip,
                      std::vector<std::uint8_t>& body, TermExit& term, std::uint32_t base,
                      std::uint32_t ram_limit, unsigned done, std::uint32_t dirty_so_far);

/// @brief Exits the block early to @p ho.rip after @p ho.done instructions.
/// Stores the dirtied locals, records the count in kExitDoneSlot and
/// returns; unlike a handoff it keeps the runner on the block path.
void emit_branch_exit(std::vector<std::uint8_t>& body, const Handoff& ho);

/// @brief Emits a void if whose condition is already on the stack.
void emit_if_void(std::vector<std::uint8_t>& body);

/**
 * @brief Full four-level page walk from kTmpV to a wrapped i32 paddr.
 * @param[in,out] body Opcode stream under construction.
 * @param[in] size Access width in bytes (1/2/4/8) for the tail bound check.
 * @param[in] ram_limit First physical address outside guest RAM.
 * @param[in] ho Handoff facts for the out-of-RAM early return.
 */
void emit_walk(std::vector<std::uint8_t>& body, unsigned size, std::uint32_t ram_limit,
               const Handoff& ho, bool write);

} // namespace micro_forge::base::detail
