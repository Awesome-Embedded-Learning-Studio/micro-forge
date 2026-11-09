/**
 * @file wasm_block.hpp
 * @brief Guest basic blocks lowered to WebAssembly function bodies.
 * @version 0.1.0
 *
 * Second JIT backend: every guest register is an i64 slot in linear
 * memory (slot = reg * 8), so a guest instruction becomes a short wasm
 * sequence. The page compiles the emitted module with WebAssembly.compile
 * and calls the exported function once per block run.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "micro_forge/arch/x86_64/decoded.hpp"

namespace micro_forge::base {

/**
 * @brief Lowers one decoded instruction to wasm body bytes.
 *
 * Supported today: mov/add/sub/and/or/xor with a register destination and
 * an imm32 source, and ret (emitted as return). Unsupported instructions
 * return nullopt and end the block.
 *
 * @param[in] insn The decoded instruction.
 * @param[in,out] body Wasm body bytes appended to.
 * @param[in,out] terminated Set when the instruction ends the block.
 * @return False when unsupported.
 */
[[nodiscard]] bool append_wasm_insn(const arch::x86_64::DecodedInsn& insn,
                                    std::vector<std::uint8_t>& body, bool& terminated);

/**
 * @brief Lowers a whole block: appends every supported instruction.
 *
 * @param[in] insns Decoded instructions in execution order.
 * @param[in,out] body Wasm body bytes (locals vector excluded).
 * @return Number of instructions lowered (the block prefix length).
 */
[[nodiscard]] std::size_t append_wasm_block(std::span<const arch::x86_64::DecodedInsn> insns,
                                            std::vector<std::uint8_t>& body);

} // namespace micro_forge::base
