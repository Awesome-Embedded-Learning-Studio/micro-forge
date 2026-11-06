/**
 * @file decode_error.hpp
 * @brief Error channel for the x86-64 decoder.
 * @version 0.1.0
 */

#pragma once

#include <cstdint>

namespace micro_forge::arch::x86_64 {

/**
 * @brief Error type for instruction decoding.
 *
 * Offset semantics: the offset is relative to the start of the byte
 * stream handed to the decoder, matching the decoder's input contract.
 */
struct DecodeError {
    /// @brief Why decoding failed.
    enum class Kind : uint8_t {
        kUnsupportedOpcode, ///< Opcode family is outside the v0 instruction set.
        kBadPrefix,         ///< Prefix combination is illegal or unsupported.
        kTruncated,         ///< Stream ended mid-instruction.
        kBadModrm,          ///< ModRM/SIB encoding is illegal (e.g. REX-less
                            ///< SIB base 101 in 32-bit addressing).
    };
    Kind kind;
    /// @brief Byte offset where the problem was detected.
    uint64_t offset;
};

} // namespace micro_forge::arch::x86_64
