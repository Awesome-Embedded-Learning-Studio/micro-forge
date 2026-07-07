/**
 * @file isa.h
 * @brief Toy ISA — 模板化指令编码/解码
 *
 * 指令格式 (32-bit fixed):
 *   [31:28] opcode    (4 bits)
 *   [23:21] Rd        (3 bits)
 *   [20:18] Rs        (3 bits)
 *   [17:15] Rt        (3 bits)
 *   [14:0]  imm15     (15-bit)
 */

#pragma once

#include "autogen/arch_details.hpp"
#include <cstdint>

namespace micro_forge::cpu::toy {

/**
 * @brief   Registers Enum, this is using to prevent
 *          Manual Import
 *
 */
enum class Reg : data_t {
    r0,
    r1,
    r2,
    r3,
    r4,
    r5,
    r6,
    r7,
};

static constexpr uint16_t RegCount = static_cast<uint16_t>(Reg::r7) + 1;

struct Field {
    data_t shift; // For Target encodings, what shift?
    data_t mask;  // Masks
};

/**
 * @brief Encoders for the instractions
 *
 * @tparam Opcode
 * @tparam Fields
 */
template <data_t Opcode, Field... Fields> struct Inst {
    template <typename... Args> static constexpr data_t encode(Args... args) {
        static_assert(sizeof...(Args) == sizeof...(Fields),
                      "mismatch operand count");
        return (Opcode << 28) |
               encode_fields<Fields...>(static_cast<data_t>(args)...);
    }

  private:
    template <Field F> static constexpr data_t encode_one(data_t value) {
        return (value & F.mask) << F.shift;
    }

    template <Field... Fs, typename... Args>
    static constexpr data_t encode_fields(Args... args) {
        if constexpr (sizeof...(Fs) == 0) {
            return 0u;
        } else {
            return (encode_one<Fs>(static_cast<data_t>(args)) | ...);
        }
    }
};

constexpr Field RD{21, 0x7u};
constexpr Field RS{18, 0x7u};
constexpr Field RT{15, 0x7u};
constexpr Field IMM15{0, 0x7FFFu};
constexpr Field IMM8{0, 0xFFu};
constexpr Field IMM5{0, 0x1Fu};
constexpr Field IRQ4{0, 0xFu};

using NOP = Inst<0x0>;
using ADD = Inst<0x1, RD, RS, RT>;
using SUB = Inst<0x2, RD, RS, RT>;
using AND = Inst<0x3, RD, RS, RT>;
using LDI = Inst<0x4, RD, IMM15>;
using LDW = Inst<0x5, RD, RS, IMM5>;
using STW = Inst<0x6, RT, RS, IMM5>;
using BZ = Inst<0x7, IMM8>;
using JMP = Inst<0x8, IMM15>;
using CALL = Inst<0x9, IMM15>;
using RET = Inst<0xA>;
using INT = Inst<0xB, IRQ4>;
using HALT = Inst<0xC>;

constexpr data_t opcode(data_t insn) {
    return (insn >> 28) & 0xFu;
}
constexpr data_t rd(data_t insn) {
    return (insn >> 21) & 0x7u;
}
constexpr data_t rs(data_t insn) {
    return (insn >> 18) & 0x7u;
}
constexpr data_t rt(data_t insn) {
    return (insn >> 15) & 0x7u;
}
constexpr data_t imm15(data_t insn) {
    return insn & 0x7FFFu;
}
constexpr data_t imm8(data_t insn) {
    return insn & 0xFFu;
}
constexpr data_t imm5(data_t insn) {
    return insn & 0x1Fu;
}
constexpr data_t imm4(data_t insn) {
    return insn & 0xFu;
}

/**
 * @brief Intr owns 16, we assume cpu support!
 *
 */
static constexpr uint16_t INTR_CNT = 16;

} // namespace micro_forge::cpu::toy
