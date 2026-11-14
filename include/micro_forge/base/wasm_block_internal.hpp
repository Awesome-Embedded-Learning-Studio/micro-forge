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

namespace micro_forge::base::detail {

/// Facts a mid-block refusal needs to hand the rest back to the runner.
struct Handoff {
    std::uint32_t base;       ///< GPR mirror base offset.
    unsigned done;            ///< instructions of this block already complete.
    std::uint64_t rip;        ///< guest rip the interpreter resumes from.
    std::uint32_t dirty_bits; ///< locals written so far that must reach the mirror.
};

/// @brief Marks the mirror handed flag and returns @p ho.rip.
/// Records done/dirty so the runner commits what already ran.
void emit_handoff(std::vector<std::uint8_t>& body, const Handoff& ho);

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
