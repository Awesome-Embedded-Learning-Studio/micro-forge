/**
 * @file wasm_block.cpp
 * @brief Guest instruction to wasm body lowering (register bridge).
 * @version 0.3.0
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
constexpr std::uint8_t kLocalGet = 0x20;
constexpr std::uint8_t kLocalSet = 0x21;
constexpr std::uint8_t kReturn = 0x0F;
constexpr std::uint8_t kAlign8 = 0x03;
// Flags-chain opcodes (parity, shifts, packing into the flags cell).
constexpr std::uint8_t kI32And = 0x71;
constexpr std::uint8_t kI32Or = 0x72;
constexpr std::uint8_t kI32Xor = 0x73;
constexpr std::uint8_t kI32Shl = 0x74;
constexpr std::uint8_t kI32ShrU = 0x76;
constexpr std::uint8_t kI64Eqz = 0x50;
constexpr std::uint8_t kI64LtU = 0x54;
constexpr std::uint8_t kI64ShrU = 0x88;
constexpr std::uint8_t kI32Wrap = 0xA7;
constexpr std::uint8_t kI32Store = 0x36;
constexpr std::uint8_t kAlign4 = 0x02;

constexpr unsigned kNumGprs = 16;         ///< One wasm local per guest GPR.
constexpr std::uint32_t kTmpA = 16;       ///< i64 scratch: original dst.
constexpr std::uint32_t kTmpB = 17;       ///< i64 scratch: source operand.
constexpr std::uint32_t kTmpP = 18;       ///< i32 scratch: parity chain.
constexpr std::uint32_t kFlagsSlot = 256; ///< i32 flags cell behind the GPRs.

/// Local index of a guest GPR, or nothing when outside the 16-GPR map.
std::optional<std::uint32_t> local_of(arch::x86_64::Reg reg) {
    const auto value = static_cast<std::uint8_t>(reg);
    if (value >= kNumGprs) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

/// i32.const value
void emit_i32(std::vector<std::uint8_t>& body, std::int32_t value) {
    body.push_back(kI32Const);
    append_sleb(body, value);
}

/// i64.const value
void emit_i64(std::vector<std::uint8_t>& body, std::int64_t value) {
    body.push_back(kI64Const);
    append_sleb(body, value);
}

/// i64.store / i64.load (align 8, offset 0)
void emit_store(std::vector<std::uint8_t>& body) {
    body.push_back(kI64Store);
    body.push_back(kAlign8);
    body.push_back(0x00);
}

void emit_load(std::vector<std::uint8_t>& body) {
    body.push_back(kI64Load);
    body.push_back(kAlign8);
    body.push_back(0x00);
}

/// local.get idx / local.set idx
void emit_local(std::vector<std::uint8_t>& body, std::uint8_t opcode, std::uint32_t idx) {
    body.push_back(opcode);
    append_uleb(body, idx);
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

/**
 * @brief Emits the qword flag chain of one ALU op into the flags cell.
 *
 * Mirrors the interpreter's alu_add/alu_sub/alu_logic (set_zsp included):
 * t_a/t_b hold the masked operands and dst holds the result. The cell
 * layout is cf|zf<<1|sf<<2|of<<3|pf<<4|af<<5, stored at kFlagsSlot.
 * @param[in,out] body Body bytes appended to.
 * @param[in] dst Destination local (holds the result).
 * @param[in] kind 0 = add, 1 = sub, 2 = logic.
 */
void emit_flags(std::vector<std::uint8_t>& body, std::uint32_t dst, unsigned kind) {
    // parity chain in t_p over the low 8 result bits
    emit_local(body, kLocalGet, dst);
    body.push_back(kI32Wrap);
    emit_local(body, kLocalSet, kTmpP);
    emit_local(body, kLocalGet, kTmpP);
    emit_local(body, kLocalGet, kTmpP);
    emit_i32(body, 4);
    body.push_back(kI32ShrU);
    body.push_back(kI32Xor);
    emit_local(body, kLocalSet, kTmpP);
    emit_local(body, kLocalGet, kTmpP);
    emit_local(body, kLocalGet, kTmpP);
    emit_i32(body, 2);
    body.push_back(kI32ShrU);
    body.push_back(kI32Xor);
    emit_local(body, kLocalSet, kTmpP);
    emit_local(body, kLocalGet, kTmpP);
    emit_local(body, kLocalGet, kTmpP);
    emit_i32(body, 1);
    body.push_back(kI32ShrU);
    body.push_back(kI32Xor);
    emit_local(body, kLocalSet, kTmpP);
    // store address first: [addr, value] is the i32.store stack order
    emit_i32(body, static_cast<std::int32_t>(kFlagsSlot));
    // value = cf (fold every further bit in with shl/or)
    if (kind == 2) {
        emit_i32(body, 0); // logic ops clear cf
    } else if (kind == 0) {
        emit_local(body, kLocalGet, dst); // add carry: res < a
        emit_local(body, kLocalGet, kTmpA);
        body.push_back(kI64LtU);
    } else {
        emit_local(body, kLocalGet, kTmpA); // sub borrow: a < b
        emit_local(body, kLocalGet, kTmpB);
        body.push_back(kI64LtU);
    }
    // zf = res == 0 (i64.eqz)
    emit_local(body, kLocalGet, dst);
    body.push_back(kI64Eqz); // zf over the i64 result
    emit_i32(body, 1);
    body.push_back(kI32Shl);
    body.push_back(kI32Or);
    // sf = (res >> 63) & 1
    emit_local(body, kLocalGet, dst);
    emit_i64(body, 63);
    body.push_back(kI64ShrU);
    body.push_back(kI32Wrap);
    emit_i32(body, 1);
    body.push_back(kI32And);
    emit_i32(body, 2);
    body.push_back(kI32Shl);
    body.push_back(kI32Or);
    // of
    if (kind == 2) {
        emit_i32(body, 0); // logic ops clear of
    } else {
        emit_local(body, kLocalGet, kTmpA);
        emit_local(body, kLocalGet, kTmpB);
        body.push_back(kI64Xor);
        if (kind == 0) { // add: (~(a^b) & (a^res)) >> 63
            emit_i64(body, -1);
            body.push_back(kI64Xor);
        }
        emit_local(body, kLocalGet, kTmpA);
        emit_local(body, kLocalGet, dst);
        body.push_back(kI64Xor);
        body.push_back(kI64And);
        emit_i64(body, 63);
        body.push_back(kI64ShrU);
        body.push_back(kI32Wrap);
        emit_i32(body, 1);
        body.push_back(kI32And);
    }
    emit_i32(body, 3);
    body.push_back(kI32Shl);
    body.push_back(kI32Or);
    // pf = even parity of the low 8 bits: 1 ^ (p & 1)
    emit_local(body, kLocalGet, kTmpP);
    emit_i32(body, 1);
    body.push_back(kI32And);
    emit_i32(body, 1);
    body.push_back(kI32Xor);
    emit_i32(body, 4);
    body.push_back(kI32Shl);
    body.push_back(kI32Or);
    // af (logic ops leave it clear, matching alu_logic)
    if (kind == 2) {
        emit_i32(body, 0);
    } else {
        emit_local(body, kLocalGet, kTmpA);
        emit_local(body, kLocalGet, kTmpB);
        body.push_back(kI64Xor);
        emit_local(body, kLocalGet, dst);
        body.push_back(kI64Xor);
        emit_i64(body, kind == 0 ? 4 : 3);
        body.push_back(kI64ShrU);
        body.push_back(kI32Wrap);
        emit_i32(body, 1);
        body.push_back(kI32And);
    }
    emit_i32(body, 5);
    body.push_back(kI32Shl);
    body.push_back(kI32Or);
    body.push_back(kI32Store);
    body.push_back(kAlign4);
    body.push_back(0x00);
}

/// Per-block register liveness: which locals must be loaded on entry
/// (used) and stored back on exit (dirtied).
class RegMask {
public:
    void mark_used(std::uint32_t local) { used_ |= 1U << local; }
    void mark_dirty(std::uint32_t local) { dirty_ |= 1U << local; }
    [[nodiscard]] bool used(std::uint32_t local) const { return (used_ & (1U << local)) != 0; }
    [[nodiscard]] bool dirty(std::uint32_t local) const { return (dirty_ & (1U << local)) != 0; }

private:
    std::uint32_t used_ = 0;
    std::uint32_t dirty_ = 0;
};

/// Two-register form (ops[0] and ops[1] both kReg, qword).
bool reg_reg(const arch::x86_64::DecodedInsn& insn) {
    return insn.ops[0].kind == Operand::Kind::kReg && insn.ops[1].kind == Operand::Kind::kReg &&
           insn.ops[0].size == 8 && insn.ops[1].size == 8;
}

/// Register, immediate form (ops[0] kReg, ops[1] kImm, qword reg).
bool reg_imm(const arch::x86_64::DecodedInsn& insn) {
    return insn.ops[0].kind == Operand::Kind::kReg && insn.ops[1].kind == Operand::Kind::kImm &&
           insn.ops[0].size == 8;
}

} // namespace

bool append_wasm_insn(const arch::x86_64::DecodedInsn& insn, std::vector<std::uint8_t>& body,
                      bool& terminated) {
    if (insn.mnemonic == Mnemonic::kRet) {
        terminated = true;
        return true; // the exit sequence is emitted by the caller
    }
    if (reg_imm(insn)) {
        const auto dst = local_of(insn.ops[0].reg);
        if (!dst.has_value()) {
            return false;
        }
        if (insn.mnemonic == Mnemonic::kMov) { // pure write: no entry load
            emit_i64(body, insn.ops[1].disp);
            emit_local(body, kLocalSet, *dst);
            return true;
        }
        const std::uint8_t opcode = alu_opcode(insn.mnemonic);
        if (opcode == 0) {
            return false;
        }
        emit_local(body, kLocalGet, *dst);
        emit_local(body, kLocalSet, kTmpA); // t_a = dst
        emit_i64(body, insn.ops[1].disp);
        emit_local(body, kLocalSet, kTmpB); // t_b = imm
        emit_local(body, kLocalGet, kTmpA);
        emit_local(body, kLocalGet, kTmpB);
        body.push_back(opcode);
        emit_local(body, kLocalSet, *dst);
        unsigned kind = 2U; // and/or/xor: logic flags
        if (insn.mnemonic == Mnemonic::kAdd) {
            kind = 0U;
        } else if (insn.mnemonic == Mnemonic::kSub) {
            kind = 1U;
        }
        emit_flags(body, *dst, kind);
        return true;
    }
    if (reg_reg(insn)) {
        const auto dst = local_of(insn.ops[0].reg);
        const auto src = local_of(insn.ops[1].reg);
        if (!dst.has_value() || !src.has_value()) {
            return false;
        }
        if (insn.mnemonic == Mnemonic::kMov) {
            emit_local(body, kLocalGet, *src);
            emit_local(body, kLocalSet, *dst);
            return true;
        }
        const std::uint8_t opcode = alu_opcode(insn.mnemonic);
        if (opcode == 0) {
            return false;
        }
        emit_local(body, kLocalGet, *dst);
        emit_local(body, kLocalSet, kTmpA);
        emit_local(body, kLocalGet, *src);
        emit_local(body, kLocalSet, kTmpB);
        emit_local(body, kLocalGet, kTmpA);
        emit_local(body, kLocalGet, kTmpB);
        body.push_back(opcode);
        emit_local(body, kLocalSet, *dst);
        unsigned kind = 2U; // and/or/xor: logic flags
        if (insn.mnemonic == Mnemonic::kAdd) {
            kind = 0U;
        } else if (insn.mnemonic == Mnemonic::kSub) {
            kind = 1U;
        }
        emit_flags(body, *dst, kind);
        return true;
    }
    return false;
}

std::size_t append_wasm_block(std::span<const arch::x86_64::DecodedInsn> insns, uint64_t base_rip,
                              std::vector<std::uint8_t>& body) {
    // Pass 1: lower into locals and collect liveness. The body starts with
    // the entry loads (emitted last, once the used set is known); the exit
    // stores and the next-rip return follow the instruction stream.
    std::vector<std::uint8_t> code;
    RegMask mask;
    std::size_t lowered = 0;
    uint64_t next_rip = base_rip;
    bool terminated = false;
    for (const auto& insn : insns) {
        bool term = false;
        const std::size_t before = code.size();
        if (!append_wasm_insn(insn, code, term)) {
            code.resize(before); // half-emitted unsupported form
            break;
        }
        if (insn.mnemonic != Mnemonic::kRet) {
            for (const auto& op : insn.ops) {
                if (op.kind == Operand::Kind::kReg) {
                    const auto local = local_of(op.reg);
                    if (local.has_value() && op.size == 8) {
                        mask.mark_used(*local);
                    }
                }
            }
            const auto dst = local_of(insn.ops[0].reg);
            if (dst.has_value()) {
                mask.mark_dirty(*dst);
            }
        }
        ++lowered;
        next_rip += insn.length;
        if (term) {
            terminated = true;
            break;
        }
    }
    if (lowered == 0) {
        return 0;
    }
    // Entry bridge: load every used GPR slot into its local.
    for (std::uint32_t local = 0; local < kNumGprs; ++local) {
        if (mask.used(local)) {
            emit_i32(body, static_cast<std::int32_t>(local * 8U));
            emit_load(body);
            emit_local(body, kLocalSet, local);
        }
    }
    body.insert(body.end(), code.begin(), code.end());
    // Exit bridge: store dirtied locals back, then return the next rip.
    // ret cannot resolve its target without a stack read: hand the
    // sentinel back and let the caller take one interpreter step.
    for (std::uint32_t local = 0; local < kNumGprs; ++local) {
        if (mask.dirty(local)) {
            emit_i32(body, static_cast<std::int32_t>(local * 8U));
            emit_local(body, kLocalGet, local);
            emit_store(body);
        }
    }
    const auto result =
        terminated ? static_cast<std::int64_t>(~UINT64_C(0)) : static_cast<std::int64_t>(next_rip);
    emit_i64(body, result);
    body.push_back(kReturn);
    return lowered;
}

std::size_t wasm_block_flags_slot() {
    return kFlagsSlot;
}

} // namespace micro_forge::base
