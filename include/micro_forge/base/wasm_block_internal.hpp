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

/// @brief Emits a void if whose condition is already on the stack.
void emit_if_void(std::vector<std::uint8_t>& body);

/// @brief Kernel-mode guard for one guest memory operand.
/// cpl != 0 or CR0.WP=1 hands the rest back to the runner; the interpreter
/// re-checks user/write permissions the block cannot mirror cheaply.
void emit_mode_guard(std::vector<std::uint8_t>& body, const Handoff& ho);

/**
 * @brief Full four-level page walk from kTmpV to a wrapped i32 paddr.
 * @param[in,out] body Opcode stream under construction.
 * @param[in] size Access width in bytes (1/2/4/8) for the tail bound check.
 * @param[in] ram_limit First physical address outside guest RAM.
 * @param[in] ho Handoff facts for the out-of-RAM early return.
 */
void emit_walk(std::vector<std::uint8_t>& body, unsigned size, std::uint32_t ram_limit,
               const Handoff& ho);

} // namespace micro_forge::base::detail
