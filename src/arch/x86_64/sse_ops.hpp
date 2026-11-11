/**
 * @file sse_ops.hpp
 * @brief Internal SSE element helpers shared by the executor files.
 * @version 0.1.0
 */

#pragma once

#include <array>
#include <cstdint>

#include "micro_forge/arch/x86_64/decoded.hpp"

namespace micro_forge::arch::x86_64::sse_ops {

/// 128-bit value as two 64-bit halves (low first).
using Vec128 = std::array<uint64_t, 2>;

[[nodiscard]] inline uint32_t elem32(const Vec128& v, unsigned i) {
    return static_cast<uint32_t>(v[i >> 1U] >> (32U * (i & 1U)));
}

[[nodiscard]] inline uint8_t byte_at(const Vec128& v, unsigned i) {
    return static_cast<uint8_t>(v[i / 8U] >> (8U * (i % 8U)));
}

[[nodiscard]] inline uint16_t elem16(const Vec128& v, unsigned i) {
    return static_cast<uint16_t>(v[i / 4U] >> (16U * (i % 4U)));
}

/// @brief Shuffles the low four words by 2-bit selectors, keeps the high.
inline void pshuflw(Vec128& dst, const Vec128& src, uint64_t ctrl) {
    for (unsigned i = 0; i < 4; ++i) {
        const auto pick = static_cast<unsigned>((ctrl >> (2U * i)) & 3U);
        const auto w = elem16(src, pick);
        dst[0] |= static_cast<uint64_t>(w) << (16U * i);
    }
    dst[1] = src[1];
}

/// @brief Shuffles the high four words, keeps the low quadword.
inline void pshufhw(Vec128& dst, const Vec128& src, uint64_t ctrl) {
    dst[0] = src[0];
    for (unsigned i = 0; i < 4; ++i) {
        const auto pick = static_cast<unsigned>((ctrl >> (2U * i)) & 3U);
        const auto w = elem16(src, 4U + pick);
        dst[1] |= static_cast<uint64_t>(w) << (16U * i);
    }
}

/**
 * @brief Writes one byte of a 128-bit value.
 *
 * @param[in,out] v The value to update.
 * @param[in] i Byte index (0..15).
 * @param[in] value The byte written.
 */
inline void put_byte(Vec128& v, unsigned i, uint8_t value) {
    const unsigned shift = 8U * (i % 8U);
    v[i / 8U] = (v[i / 8U] & ~(0xFFULL << shift)) | (static_cast<uint64_t>(value) << shift);
}

/**
 * @brief Packed integer add/sub with element width from the mnemonic.
 *
 * @param[in] mnemonic The add/sub mnemonic.
 * @param[in,out] dst Destination register value.
 * @param[in] src Source register value.
 */
inline void pack_add_sub(Mnemonic mnemonic, Vec128& dst, const Vec128& src) {
    if (mnemonic == Mnemonic::kPaddq || mnemonic == Mnemonic::kPsubq) {
        const bool add = mnemonic == Mnemonic::kPaddq;
        dst[0] = add ? dst[0] + src[0] : dst[0] - src[0];
        dst[1] = add ? dst[1] + src[1] : dst[1] - src[1];
        return;
    }
    unsigned elem_bytes = 4;
    bool add = false;
    switch (mnemonic) {
    case Mnemonic::kPaddd:
        elem_bytes = 4;
        add = true;
        break;
    case Mnemonic::kPsubd:
        elem_bytes = 4;
        add = false;
        break;
    case Mnemonic::kPaddb:
        elem_bytes = 1;
        add = true;
        break;
    case Mnemonic::kPsubb:
        elem_bytes = 1;
        add = false;
        break;
    case Mnemonic::kPaddw:
        elem_bytes = 2;
        add = true;
        break;
    default:
        elem_bytes = 2;
        add = false;
        break;
    }
    const unsigned per = 8U / elem_bytes;
    const uint64_t mask = (1ULL << (8U * elem_bytes)) - 1ULL;
    for (unsigned half = 0; half < 2; ++half) {
        uint64_t word = 0;
        for (unsigned k = 0; k < per; ++k) {
            const unsigned shift = k * 8U * elem_bytes;
            const uint64_t lhs = (dst[half] >> shift) & mask;
            const uint64_t rhs = (src[half] >> shift) & mask;
            const uint64_t val = (add ? (lhs + rhs) : (lhs - rhs)) & mask;
            word |= val << shift;
        }
        dst[half] = word;
    }
}

/**
 * @brief Packed byte/word min/max (0F DA/DE/EA/EE).
 *
 * @param[in] mnemonic The min/max mnemonic.
 * @param[in,out] dst Destination register value.
 * @param[in] src Source register value.
 */
inline void pack_min_max(Mnemonic mnemonic, Vec128& dst, const Vec128& src) {
    const bool word = mnemonic == Mnemonic::kPminsw || mnemonic == Mnemonic::kPmaxsw;
    const bool low = mnemonic == Mnemonic::kPminub || mnemonic == Mnemonic::kPminsw;
    const unsigned elem_bytes = word ? 2U : 1U;
    const unsigned per = 8U / elem_bytes;
    const uint64_t mask = word ? 0xFFFFULL : 0xFFULL;
    for (unsigned half = 0; half < 2; ++half) {
        uint64_t word_out = 0;
        for (unsigned k = 0; k < per; ++k) {
            const unsigned shift = k * 8U * elem_bytes;
            const uint64_t lhs = (dst[half] >> shift) & mask;
            const uint64_t rhs = (src[half] >> shift) & mask;
            bool take_lhs = false;
            if (word) {
                const auto a16 = static_cast<int16_t>(lhs);
                const auto b16 = static_cast<int16_t>(rhs);
                take_lhs = low ? (a16 <= b16) : (a16 >= b16);
            } else {
                take_lhs = low ? (lhs <= rhs) : (lhs >= rhs);
            }
            const uint64_t val = take_lhs ? lhs : rhs;
            word_out |= (val & mask) << shift;
        }
        dst[half] = word_out;
    }
}

/**
 * @brief PSADBW (66 0F F6): per 8-byte lane, sum of unsigned byte
 * absolute differences, widened into the lane's low 16 bits.
 *
 * @param[in,out] dst Destination register value (also the left operand).
 * @param[in] src Source register value.
 */
inline void psadbw(Vec128& dst, const Vec128& src) {
    for (unsigned half = 0; half < 2; ++half) {
        unsigned sum = 0; // at most 8 * 255 = 2040: fits 16 bits
        for (unsigned k = 0; k < 8; ++k) {
            const auto lhs = static_cast<unsigned>(dst[half] >> (8U * k)) & 0xFFU;
            const auto rhs = static_cast<unsigned>(src[half] >> (8U * k)) & 0xFFU;
            sum += (lhs > rhs) ? (lhs - rhs) : (rhs - lhs);
        }
        dst[half] = sum;
    }
}

} // namespace micro_forge::arch::x86_64::sse_ops
