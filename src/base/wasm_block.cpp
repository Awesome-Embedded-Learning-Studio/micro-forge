/**
 * @file wasm_block.cpp
 * @brief Guest instruction to wasm body lowering.
 * @version 0.1.0
 */

#include "micro_forge/base/wasm_block.hpp"

#include <array>

#include "micro_forge/base/wasm_emit.hpp"

namespace micro_forge::base {

namespace {

using arch::x86_64::Mnemonic;
using arch::x86_64::Operand;

// wasm opcodes used by the slice.
constexpr std::uint8_t kI32Const = 0x41;
constexpr std::uint8_t kI64Const = 0x42;
constexpr std::uint8_t kI64Load = 0x29;
constexpr std::uint8_t kI64Store = 0x37;
constexpr std::uint8_t kI64Add = 0x7C;
constexpr std::uint8_t kI64Sub = 0x7D;
constexpr std::uint8_t kI64And = 0x83;
constexpr std::uint8_t kI64Or = 0x84;
constexpr std::uint8_t kI64Xor = 0x85;
constexpr std::uint8_t kReturn = 0x0F;
constexpr std::uint8_t kAlign8 = 0x03;

/// Slot of a guest GPR inside linear memory (reg * 8).
std::optional<std::uint32_t> slot_of(arch::x86_64::Reg reg) {
    const auto value = static_cast<std::uint8_t>(reg);
    if (value >= 16) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value) * 8U;
}

/// i32.const slot
void emit_slot(std::vector<std::uint8_t>& body, std::uint32_t slot) {
    body.push_back(kI32Const);
    append_uleb(body, slot);
}

/// i64.const value
void emit_imm(std::vector<std::uint8_t>& body, std::int64_t value) {
    body.push_back(kI64Const);
    append_sleb(body, value);
}

/// i64.store (align 8, offset 0)
void emit_store(std::vector<std::uint8_t>& body) {
    body.push_back(kI64Store);
    body.push_back(kAlign8);
    body.push_back(0x00);
}

/// i64.load (align 8, offset 0)
void emit_load(std::vector<std::uint8_t>& body) {
    body.push_back(kI64Load);
    body.push_back(kAlign8);
    body.push_back(0x00);
}

/// The arithmetic opcode for a mnemonic, or 0 when not in the slice.
std::uint8_t alu_opcode(Mnemonic mnemonic) {
    switch (mnemonic) {
    case Mnemonic::kAdd:
        return kI64Add;
    case Mnemonic::kSub:
        return kI64Sub;
    case Mnemonic::kAnd:
        return kI64And;
    case Mnemonic::kOr:
        return kI64Or;
    case Mnemonic::kXor:
        return kI64Xor;
    default:
        return 0;
    }
}

} // namespace

bool append_wasm_insn(const arch::x86_64::DecodedInsn& insn, std::vector<std::uint8_t>& body,
                      bool& terminated) {
    if (insn.mnemonic == Mnemonic::kRet) {
        body.push_back(kReturn);
        terminated = true;
        return true;
    }
    if (insn.ops[0].kind != Operand::Kind::kReg || insn.ops[1].kind != Operand::Kind::kImm ||
        insn.ops[0].size != 8) {
        return false;
    }
    const auto slot = slot_of(insn.ops[0].reg);
    if (!slot.has_value()) {
        return false;
    }
    if (insn.mnemonic == Mnemonic::kMov) {
        emit_slot(body, *slot);
        emit_imm(body, insn.ops[1].disp);
        emit_store(body);
        return true;
    }
    const std::uint8_t opcode = alu_opcode(insn.mnemonic);
    if (opcode == 0) {
        return false;
    }
    emit_slot(body, *slot);
    emit_slot(body, *slot);
    emit_load(body);
    emit_imm(body, insn.ops[1].disp);
    body.push_back(opcode);
    emit_store(body);
    return true;
}

std::size_t append_wasm_block(std::span<const arch::x86_64::DecodedInsn> insns,
                              std::vector<std::uint8_t>& body) {
    std::size_t lowered = 0;
    for (const auto& insn : insns) {
        bool terminated = false;
        if (!append_wasm_insn(insn, body, terminated)) {
            break;
        }
        ++lowered;
        if (terminated) {
            break;
        }
    }
    return lowered;
}

} // namespace micro_forge::base
