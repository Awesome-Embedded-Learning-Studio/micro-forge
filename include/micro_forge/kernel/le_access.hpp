/**
 * @file le_access.hpp
 * @brief Little-endian multi-byte reads over raw byte pointers (promoted
 *        from ram_region.cpp's file-local helpers when the decoder became
 *        the second consumer, per the batch-1 notes).
 * @version 0.1.0
 */

#pragma once

#include <cstdint>

namespace micro_forge::kernel {

/**
 * @brief Reads a little-endian 16-bit value at @p p.
 *
 * @param[in] p Byte address; the caller guarantees p[0] and p[1] are
 *            readable (bus regions already validated the access).
 * @return The value assembled least-significant byte first.
 */
[[nodiscard]] inline uint16_t get_le16(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                                 static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8U));
}

/**
 * @brief Reads a little-endian 32-bit value at @p p.
 *
 * @param[in] p Byte address; four readable bytes required.
 * @return The value assembled least-significant byte first.
 */
[[nodiscard]] inline uint32_t get_le32(const uint8_t* p) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) {
        v |= static_cast<uint32_t>(p[i]) << (8U * i);
    }
    return v;
}

/**
 * @brief Reads a little-endian 64-bit value at @p p.
 *
 * @param[in] p Byte address; eight readable bytes required.
 * @return The value assembled least-significant byte first.
 */
[[nodiscard]] inline uint64_t get_le64(const uint8_t* p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i) {
        v |= static_cast<uint64_t>(p[i]) << (8U * i);
    }
    return v;
}

/**
 * @brief Stores a little-endian 16-bit value at @p p.
 *
 * @param[out] p Byte address; two writable bytes required.
 * @param[in]  value Value to store.
 */
inline void put_le16(uint8_t* p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8U);
}

/**
 * @brief Stores a little-endian 32-bit value at @p p.
 *
 * @param[out] p Byte address; four writable bytes required.
 * @param[in]  value Value to store.
 */
inline void put_le32(uint8_t* p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        p[i] = static_cast<uint8_t>(value >> (8U * i));
    }
}

/**
 * @brief Stores a little-endian 64-bit value at @p p.
 *
 * @param[out] p Byte address; eight writable bytes required.
 * @param[in]  value Value to store.
 */
inline void put_le64(uint8_t* p, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) {
        p[i] = static_cast<uint8_t>(value >> (8U * i));
    }
}

} // namespace micro_forge::kernel
