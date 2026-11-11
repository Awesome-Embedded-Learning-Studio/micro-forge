/**
 * @file mul_wide.hpp
 * @brief Wide multiply primitives shared by the interpreter (grp3) and the
 *        JIT helpers, so both sides compute identical high halves.
 */

#ifndef MICRO_FORGE_ARCH_X86_64_MUL_WIDE_HPP
#define MICRO_FORGE_ARCH_X86_64_MUL_WIDE_HPP

#include <cstdint>

namespace micro_forge::arch::x86_64 {

/**
 * @brief Full 64x64 -> 128 unsigned product without __int128.
 *
 * wasm32 has no native 128-bit integers, so the product is built from
 * 32-bit half decomposition. Both the interpreter and JIT use this so
 * imul's CF/OF come out bit-identical.
 *
 * @param[in] a multiplicand.
 * @param[in] b multiplier.
 * @param[out] high receives bits 64..127 of the product.
 * @return uint64_t bits 0..63 of the product.
 */
inline uint64_t mul_wide(uint64_t a, uint64_t b, uint64_t& high) {
    constexpr uint64_t k_half = 0xFFFFFFFFULL;
    const auto a_lo = static_cast<uint32_t>(a & k_half);
    const auto a_hi = static_cast<uint32_t>(a >> 32U);
    const auto b_lo = static_cast<uint32_t>(b & k_half);
    const auto b_hi = static_cast<uint32_t>(b >> 32U);
    const uint64_t p0 = static_cast<uint64_t>(a_lo) * b_lo;
    const uint64_t p1 = static_cast<uint64_t>(a_lo) * b_hi;
    const uint64_t p2 = static_cast<uint64_t>(a_hi) * b_lo;
    const uint64_t p3 = static_cast<uint64_t>(a_hi) * b_hi;
    const uint64_t mid = (p0 >> 32U) + (p1 & k_half) + (p2 & k_half);
    const uint64_t low = (mid << 32U) | (p0 & k_half);
    high = p3 + (p1 >> 32U) + (p2 >> 32U) + (mid >> 32U);
    return low;
}

/**
 * @brief Signed high half of a 64x64 multiply (two's-complement identity).
 * @param[in] a multiplicand.
 * @param[in] b multiplier.
 * @return int64_t bits 64..127 of the signed product.
 */
inline int64_t imul_high(int64_t a, int64_t b) {
    uint64_t high = 0;
    mul_wide(static_cast<uint64_t>(a), static_cast<uint64_t>(b), high);
    if (a < 0) {
        high -= static_cast<uint64_t>(b);
    }
    if (b < 0) {
        high -= static_cast<uint64_t>(a);
    }
    return static_cast<int64_t>(high);
}

/**
 * @brief Sign-fill high word expected when a 64-bit signed product fits its
 *        low half; the arithmetic shift is the point.
 * @param[in] low bits 0..63 of the product.
 * @return int64_t bits 64..127 under the "fits" hypothesis.
 */
inline int64_t sign_fill_of(uint64_t low) {
    return static_cast<int64_t>(low) >> 63; // NOLINT(bugprone-signed-bitwise)
}

} // namespace micro_forge::arch::x86_64

#endif // MICRO_FORGE_ARCH_X86_64_MUL_WIDE_HPP
