#pragma once

#include <cstdint>
namespace micro_forge::cpu {
namespace arm::cortex_m3::thumb {

constexpr uint8_t dn(uint16_t insn) {
    return (insn >> 7) & 0x1;
}

/**
 * @brief Extract the Destination Register, bitmask the low 3 bits
 *
 * @param instruction
 * @return constexpr uint8_t
 */
constexpr uint8_t rd3(uint16_t instruction) {
    return instruction & 0b111;
}

constexpr uint8_t rd4(uint16_t insn) {
    return (dn(insn) << 3) | rd3(insn);
}

/**
 * @brief MOV/CMP/ADD/SUB immediate's Rd/Rn
 *
 * @param insn
 * @return constexpr uint8_t
 */
constexpr uint8_t rd8(uint16_t insn) {
    return (insn >> 8) & 0b111;
}

/**
 * @brief Extract the first op_num, bitmask the middle 3 bits
 *
 * @param instruction
 * @return constexpr uint8_t
 */
constexpr uint8_t rn3(uint16_t instruction) {
    return (instruction >> 3) & 0b111;
}

/**
 * @brief Extract the second op_num, bitmask the middle 3 bits
 *
 * @param instruction
 * @return constexpr uint8_t
 */
constexpr uint8_t rm3(uint16_t instruction) {
    return (instruction >> 6) & 0b111;
}

constexpr uint8_t rm4(uint16_t insn) {
    return (insn >> 3) & 0b1111;
}

constexpr uint16_t imm11(uint16_t insn) {
    return insn & 0x7FF;
}

constexpr uint8_t imm8(uint16_t instruction) {
    return instruction & 0xFFu;
}
constexpr uint8_t imm5(uint16_t insn) {
    return (insn >> 6) & 0b11111;
}
constexpr uint8_t cond(uint16_t insn) {
    return (insn >> 8) & 0b1111;
}

constexpr bool m_bit(uint16_t insn) {
    return (insn >> 8) & 0x1u;
}

/**
 * @brief Get the Register Lists
 *
 * @param insn
 * @return constexpr uint8_t
 */
constexpr uint8_t reg_list(uint16_t insn) {
    return insn & 0xFFu;
}

constexpr bool is_32bit_prefix_instruction(uint16_t hw1) {
    return (hw1 & 0xE000) == 0xE000 && (hw1 & 0x1800) != 0;
}

/// Extract bits[15:11] as main decode key (0-31)
constexpr uint8_t decode_key(uint16_t insn) {
    return (insn >> 11) & 0x1Fu;
}

/// ARMv7-M barrel shift, returning both the shifted value and the shifter
/// carry-out (the C flag source for shift instructions). Pure model of the
/// ARM shift operation — no CPU state.
///
/// type: 0=LSL, 1=LSR, 2=ASR, 3=ROR.
/// `amount` is the RESOLVED shift amount: for immediate shifts the caller
/// converts the encoded field (LSR/ASR/ROR encoded 0 → 32; LSL encoded 0 → 0).
/// `carry_in` is the current C flag, used when amount==0 (LSL/LSR/ASR leave C
/// unchanged; ROR #0 is RRX).
struct ShiftOut {
    uint32_t value;
    bool carry;
};

inline ShiftOut barrel_shift(uint8_t type, uint32_t value, uint8_t amount,
                             bool carry_in) {
    if (amount == 0) {
        if (type == 3) { // ROR #0 → RRX: (C:value) >> 1, C = value[0]
            return {(static_cast<uint32_t>(carry_in) << 31) | (value >> 1),
                    (value & 1u) != 0};
        }
        return {value, carry_in}; // LSL/LSR/ASR #0 → unchanged, C unchanged
    }
    switch (type) {
        case 0: { // LSL
            if (amount >= 32) {
                return {0u, amount == 32 ? ((value & 1u) != 0) : false};
            }
            return {value << amount, ((value >> (32 - amount)) & 1u) != 0};
        }
        case 1: { // LSR
            if (amount >= 32) {
                return {0u, (value & 0x80000000u) != 0};
            }
            return {value >> amount, ((value >> (amount - 1)) & 1u) != 0};
        }
        case 2: { // ASR
            if (amount >= 32) {
                bool sign = (value & 0x80000000u) != 0;
                return {sign ? 0xFFFFFFFFu : 0u, sign};
            }
            return {static_cast<uint32_t>(static_cast<int32_t>(value) >> amount),
                    ((value >> (amount - 1)) & 1u) != 0};
        }
        default: { // ROR (amount > 0)
            uint8_t r = amount & 31u;
            if (r == 0) {
                return {value, (value & 0x80000000u) != 0};
            }
            return {(value >> r) | (value << (32 - r)),
                    ((value >> (r - 1)) & 1u) != 0};
        }
    }
}

} // namespace arm::cortex_m3::thumb
} // namespace micro_forge::cpu
