/**
 * @file decoder.hpp
 * @brief Table-driven x86-64 decoder for the v0 instruction set.
 * @version 0.1.0
 */

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

#include "decode_error.hpp"
#include "decoded.hpp"
#include "micro_forge/arch/x86_64/decode_error.hpp"
#include "micro_forge/arch/x86_64/decoded.hpp"

namespace micro_forge::arch::x86_64 {

/**
 * @brief Decodes one instruction from the head of @p bytes.
 *
 * @param[in] bytes Remaining byte stream; decoding stops at the end of
 *            one instruction. The stream may be truncated; that reports
 *            kTruncated rather than reading past the end.
 * @return The decoded instruction (its @c length says how many bytes were
 *         consumed), or a DecodeError with a stream-relative offset.
 */
[[nodiscard]] std::expected<DecodedInsn, DecodeError> decode_one(std::span<const uint8_t> bytes);

/// @brief Canonical mnemonic text ("mov", "ja", ...) for tests and dumps.
[[nodiscard]] std::string_view mnemonic_name(Mnemonic mn);

/// @brief Register text for a GPR at a given byte width ("rax", "eax",
///        "ax", "al"). @p rex_present disambiguates spl/bpl/sil/dil from
///        ah/ch/dh/bh at width 1. Only GPRs are valid here.
[[nodiscard]] std::string_view gpr_name(Reg reg, uint8_t size, bool rex_present);

/**
 * @brief One-line Intel-syntax text for a decoded instruction.
 *
 * @param[in] insn Decoded instruction.
 * @param[in] rip  Offset of the instruction start in the byte stream;
 *            branch targets are printed as rip + length + displacement.
 * @return Text like "mov rax, QWORD PTR [rbx+rcx*4+0x10]".
 */
[[nodiscard]] std::string to_text(const DecodedInsn& insn, uint64_t rip);

} // namespace micro_forge::arch::x86_64
