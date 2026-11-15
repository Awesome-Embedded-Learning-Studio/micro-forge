#pragma once
#include "thumb_fields.hpp"
#include <cstdint>

namespace micro_forge::arch {
namespace arm::cortex_m3::thumb32 {
constexpr uint32_t s_bit(uint16_t hw1) {
    return (hw1 >> 10) & 0x1u;
}

constexpr uint16_t hw1_imm10(uint16_t hw1) {
    return hw1 & 0x3FFu;
}

constexpr uint16_t hw2_imm11(uint16_t hw2) {
    return hw2 & 0x7FFu;
}

// Get the bits[13]
constexpr uint32_t j1(uint16_t hw2) {
    return (hw2 >> 13) & 0x1u;
}

// Get the bits[11]
constexpr uint32_t j2(uint16_t hw2) {
    return (hw2 >> 11) & 0x1u;
}

constexpr uint32_t mov_i(uint16_t hw1) {
    return (hw1 >> 10) & 0x1u;
}

constexpr uint32_t mov_imm4(uint16_t hw1) {
    return hw1 & 0xFu;
}

constexpr uint32_t mov_imm3(uint16_t hw2) {
    return (hw2 >> 12) & 0x7u;
}

constexpr uint8_t hw2_rd4(uint16_t hw2) {
    return (hw2 >> 8) & 0xFu;
}

constexpr uint16_t decode_imm16(uint16_t hw1, uint16_t hw2) {
    return static_cast<uint16_t>((mov_imm4(hw1) << 12) | (mov_i(hw1) << 11) | (mov_imm3(hw2) << 8) |
                                 thumb::imm8(hw2));
}

constexpr uint8_t decode_key(uint16_t insn) {
    return (insn >> 11) & 0x1Fu;
}

// Thumb-2 modified immediate: expand imm12 → imm32
// imm12 = i:imm3:imm8 where i=hw1[10], imm3=hw2[14:12], imm8=hw2[7:0]
constexpr uint32_t expand_imm12(uint32_t i, uint32_t imm3, uint32_t imm8) {
    uint32_t imm12 = (i << 11) | (imm3 << 8) | imm8;

    // Case: imm12[11:10] == 00 → simple patterns based on imm3
    if ((imm12 >> 10) == 0) {
        uint32_t suffix = imm12 & 0xFF;
        switch (imm3) {
        case 0:
            return suffix;
        case 1:
            return (suffix << 16) | suffix;
        case 2:
            return (suffix << 24) | (suffix << 8);
        case 3:
            return (suffix << 24) | (suffix << 16) | (suffix << 8) | suffix;
        default:
            return 0; // unreachable for 2-bit imm3 when i=0
        }
    }

    // Rotation case: val = 0x80 | imm12[6:0], ROR by imm12[11:7]
    uint32_t val = 0x80 | (imm12 & 0x7F);
    uint32_t rot = (imm12 >> 7) & 0x1F;
    return rot == 0 ? val : ((val >> rot) | (val << (32 - rot)));
}

// Data processing immediate helpers
constexpr uint8_t dp_rn(uint16_t hw1) {
    return hw1 & 0xF;
}
constexpr uint8_t dp_rd(uint16_t hw2) {
    return (hw2 >> 8) & 0xF;
}

} // namespace arm::cortex_m3::thumb32
} // namespace micro_forge::arch
