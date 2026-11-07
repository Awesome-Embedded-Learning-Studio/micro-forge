/**
 * @file bit.hpp
 * @brief Named bit-field vocabulary: positions hand-written, masks derived.
 * @version 0.1.0
 *
 * Shape borrowed from Cinux base's bit_ops/bitmask.hpp (same maintainer,
 * MIT): bit positions are the only thing written by hand, and every mask
 * is derived from a position, so the position-to-value translation never
 * happens inside a human head. micro-forge keeps its own small copy
 * rather than including across repositories; the two trees stay
 * independent and never drift through a shared root.
 */

#pragma once

#include <cstdint>

namespace micro_forge::kernel::bit {

/// @brief Where a field lives inside a word.
struct BitRange {
    uint8_t low;   ///< Lowest bit of the field, zero-based from bit zero.
    uint8_t width; ///< Field width in bits.
};

/**
 * @brief Ones across @p width bits, right-justified.
 *
 * @param[in] width Bit count; 64 and above saturate to the full word.
 * @return The width-wide mask.
 */
[[nodiscard]] constexpr uint64_t ones(unsigned width) {
    return width >= 64U ? ~0ULL : (1ULL << width) - 1ULL;
}

/**
 * @brief Single-bit mask at @p position.
 *
 * @param[in] position Bit index, zero-based from the least significant bit.
 * @return A mask with exactly that bit set.
 */
[[nodiscard]] constexpr uint64_t mask_bit(unsigned position) {
    return 1ULL << position;
}

/**
 * @brief Reads a field: shift down by low, keep width bits.
 *
 * @param[in] word Word holding the field.
 * @param[in] range Field location inside the word.
 * @return The field value, right-justified.
 */
[[nodiscard]] constexpr uint64_t extract(uint64_t word, BitRange range) {
    return (word >> range.low) & ones(range.width);
}

/**
 * @brief Writes a field: clear the span, place the value, neighbours
 *        keep their bits.
 *
 * @param[in] word Original word.
 * @param[in] range Field location inside the word.
 * @param[in] value Right-justified field value; bits beyond the width
 *            are dropped.
 * @return The word with the field deposited.
 */
[[nodiscard]] constexpr uint64_t deposit(uint64_t word, BitRange range, uint64_t value) {
    const uint64_t placed = ones(range.width) << range.low;
    return (word & ~placed) | ((value << range.low) & placed);
}

} // namespace micro_forge::kernel::bit
