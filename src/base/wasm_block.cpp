/**
 * @file wasm_block.cpp
 * @brief Guest instruction to wasm body lowering (register bridge).
 * @version 0.3.0
 */

#include "micro_forge/base/wasm_block.hpp"

#include <array>
#include <optional>
#include <span>

#include "micro_forge/base/wasm_block_detail.hpp"
#include "micro_forge/base/wasm_emit.hpp"

namespace micro_forge::base {

namespace {

using namespace detail; // NOLINT(google-build-using-namespace)

using arch::x86_64::Mnemonic;
using arch::x86_64::Operand;

/// Per-local liveness across one block (used on entry, stored on exit).
struct RegMask {
    std::uint32_t used = 0;  ///< GPR locals read by the body.
    std::uint32_t dirty = 0; ///< GPR locals written by the body.

    void mark_used(std::uint32_t local) { used |= 1U << local; }
    void mark_dirty(std::uint32_t local) { dirty |= 1U << local; }
    [[nodiscard]] bool used_ok(std::uint32_t local) const { return (used & (1U << local)) != 0U; }
    [[nodiscard]] bool dirty_ok(std::uint32_t local) const { return (dirty & (1U << local)) != 0U; }
    [[nodiscard]] std::uint32_t mask_used() const { return used; }
    [[nodiscard]] std::uint32_t mask_dirty() const { return dirty; }
};

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

/// Two-register form (ops[0] and ops[1] both kReg, qword or dword).
bool reg_reg(const arch::x86_64::DecodedInsn& insn) {
    return insn.ops[0].kind == Operand::Kind::kReg && insn.ops[1].kind == Operand::Kind::kReg &&
           (insn.ops[0].size == 8 || insn.ops[0].size == 4) && insn.ops[1].size == insn.ops[0].size;
}

/// Register, immediate form (ops[0] kReg, ops[1] kImm, qword or dword reg).
bool reg_imm(const arch::x86_64::DecodedInsn& insn) {
    return insn.ops[0].kind == Operand::Kind::kReg && insn.ops[1].kind == Operand::Kind::kImm &&
           (insn.ops[0].size == 8 || insn.ops[0].size == 4);
}

/// i32.load of the flags cell (leaves the packed flags on the stack).
void emit_load_flags(std::vector<std::uint8_t>& body, std::uint32_t base) {
    emit_i32(body, static_cast<std::int32_t>(kFlagsSlot + base));
    body.push_back(kI32Load);
    body.push_back(kAlign4);
    body.push_back(0x00);
}

/// (flags >> bit) & 1 for bit in 0..5.
void emit_flag_bit(std::vector<std::uint8_t>& body, unsigned bit, std::uint32_t base) {
    emit_load_flags(body, base);
    emit_i32(body, static_cast<std::int32_t>(bit));
    body.push_back(kI32ShrU);
    emit_i32(body, 1);
    body.push_back(kI32And);
}

/**
 * @brief Emits the condition of one jcc as an exact i32 boolean.
 *
 * Mirrors Cpu::condition over the packed flags cell
 * (cf|zf<<1|sf<<2|of<<3|pf<<4|af<<5). Compound codes combine the raw
 * bits before the trailing eqz, so every expression is an exact 0/1.
 */
// NOLINTNEXTLINE(misc-no-recursion) -- kLe/kGe reuse the kL chain
void emit_cc(std::vector<std::uint8_t>& body, arch::x86_64::Cc cc, std::uint32_t base) {
    using arch::x86_64::Cc;
    switch (cc) {
    case Cc::kO:
        emit_flag_bit(body, 3, base);
        return;
    case Cc::kNo:
        emit_flag_bit(body, 3, base);
        break;
    case Cc::kB:
        emit_flag_bit(body, 0, base);
        return;
    case Cc::kAe:
        emit_flag_bit(body, 0, base);
        break;
    case Cc::kE:
        emit_flag_bit(body, 1, base);
        return;
    case Cc::kNe:
        emit_flag_bit(body, 1, base);
        break;
    case Cc::kBe: // cf | zf: nonzero cell bits under mask 3
        emit_load_flags(body, base);
        emit_i32(body, 3);
        body.push_back(kI32And);
        return;
    case Cc::kA:
        emit_load_flags(body, base);
        emit_i32(body, 3);
        body.push_back(kI32And);
        break;
    case Cc::kS:
        emit_flag_bit(body, 2, base);
        return;
    case Cc::kNs:
        emit_flag_bit(body, 2, base);
        break;
    case Cc::kP:
        emit_flag_bit(body, 4, base);
        return;
    case Cc::kNp:
        emit_flag_bit(body, 4, base);
        break;
    case Cc::kL: // (sf ^ of) & 1
        emit_load_flags(body, base);
        emit_i32(body, 2);
        body.push_back(kI32ShrU);
        emit_load_flags(body, base);
        emit_i32(body, 3);
        body.push_back(kI32ShrU);
        body.push_back(kI32Xor);
        emit_i32(body, 1);
        body.push_back(kI32And);
        return;
    case Cc::kGe:
        emit_cc(body, Cc::kL, base);
        break;
    case Cc::kLe: // zf | ((sf ^ of) & 1)
        emit_flag_bit(body, 1, base);
        emit_cc(body, Cc::kL, base);
        body.push_back(kI32Or);
        return;
    case Cc::kG:
        emit_flag_bit(body, 1, base);
        emit_cc(body, Cc::kL, base);
        body.push_back(kI32Or);
        break;
    }
    body.push_back(kI32Eqz); // the negated codes fall through to here
}

/// Mnemonics that write ops[0] (cmp/test only consume their operands).
bool writes_dst(Mnemonic mnemonic) {
    return mnemonic == Mnemonic::kMov || alu_opcode(mnemonic) != 0;
}

/// Mnemonics whose lowering calls emit_flags (writes the flags cell).
bool writes_flags(Mnemonic mnemonic) {
    return alu_opcode(mnemonic) != 0 || mnemonic == Mnemonic::kCmp || mnemonic == Mnemonic::kTest;
}

/// Exit shape one terminating instruction reports to the block builder.
struct TermExit {
    WasmTerm kind = WasmTerm::kNext;
    std::uint64_t taken = 0;
    std::uint64_t fall = 0;
};

/// ALU flag family of a mnemonic (0 add, 1 sub, 2 logic).
unsigned alu_kind(Mnemonic mnemonic) {
    if (mnemonic == Mnemonic::kAdd) {
        return 0U;
    }
    return mnemonic == Mnemonic::kSub || mnemonic == Mnemonic::kCmp ? 1U : 2U;
}

/// Effective ALU opcode including cmp (sub) and test (and).
std::uint8_t cmp_alu_opcode(Mnemonic mnemonic) {
    if (mnemonic == Mnemonic::kCmp) {
        return kI64Sub;
    }
    if (mnemonic == Mnemonic::kTest) {
        return kI64And;
    }
    return alu_opcode(mnemonic);
}

/**
 * @brief Lowers one decoded instruction into wasm body bytes.
 *
 * Supported: mov/add/sub/and/or/xor/cmp/test with a qword register
 * destination and a register or imm32 source; jmp imm and jcc (block
 * terminators reporting their targets); ret (terminator whose stack read
 * stays with the interpreter step).
 */
bool lower_insn(const arch::x86_64::DecodedInsn& insn, std::uint64_t rip,
                std::vector<std::uint8_t>& body, TermExit& term, std::uint32_t base,
                std::uint32_t ram_limit, unsigned done, std::uint32_t dirty_so_far) {
    const std::uint64_t after = rip + insn.length;
    if (insn.mnemonic == Mnemonic::kRet) {
        term.kind = WasmTerm::kStep;
        term.taken = rip; // handoff exit resumes the interpreter here
        return true;      // the exit sequence is emitted by the caller
    }
    if (insn.mnemonic == Mnemonic::kJmp && insn.ops[0].kind == Operand::Kind::kImm) {
        term.kind = WasmTerm::kJump;
        term.taken = after + static_cast<std::uint64_t>(insn.ops[0].disp);
        return true;
    }
    if (insn.mnemonic == Mnemonic::kJcc && insn.ops[0].kind == Operand::Kind::kImm) {
        term.kind = WasmTerm::kBranch;
        term.taken = after + static_cast<std::uint64_t>(insn.ops[0].disp);
        term.fall = after;
        return true;
    }
    if (insn.ops[0].kind == Operand::Kind::kMem || insn.ops[1].kind == Operand::Kind::kMem) {
        return lower_mem_insn(insn, rip, body, base, ram_limit, done, dirty_so_far);
    }
    const bool qword = insn.ops[0].size == 8;
    const std::int64_t width_mask =
        qword ? ~INT64_C(0) : static_cast<std::int64_t>(UINT64_C(0xFFFFFFFF));
    const bool cmp_test = insn.mnemonic == Mnemonic::kCmp || insn.mnemonic == Mnemonic::kTest;
    const bool imm_src = reg_imm(insn);
    const bool reg_src = reg_reg(insn);
    if ((!imm_src && !reg_src) ||
        (!cmp_test && insn.mnemonic != Mnemonic::kMov && !writes_flags(insn.mnemonic))) {
        return false;
    }
    const auto dst = local_of(insn.ops[0].reg);
    const auto src = imm_src ? std::optional<std::uint32_t>{} : local_of(insn.ops[1].reg);
    if (!dst.has_value() || (!imm_src && !src.has_value())) {
        return false;
    }
    if (insn.mnemonic == Mnemonic::kMov) {
        if (imm_src) {
            // NOLINTNEXTLINE(bugprone-signed-bitwise) -- dword zero-extend mask
            emit_i64(body, insn.ops[1].disp & width_mask); // mov r32 zero-extends
        } else {
            emit_local(body, kLocalGet, *src);
            if (!qword) {
                emit_i64(body, width_mask);
                body.push_back(kI64And);
            }
        }
        emit_local(body, kLocalSet, *dst);
        return true;
    }
    const std::uint8_t opcode = cmp_alu_opcode(insn.mnemonic);
    emit_local(body, kLocalGet, *dst);
    if (!qword) {
        emit_i64(body, width_mask);
        body.push_back(kI64And); // t_a must match the 32-bit domain too
    }
    emit_local(body, kLocalSet, kTmpA); // t_a = dst
    if (imm_src) {
        // NOLINTNEXTLINE(bugprone-signed-bitwise) -- dword operand mask
        emit_i64(body, insn.ops[1].disp & width_mask);
    } else {
        emit_local(body, kLocalGet, *src);
        emit_i64(body, width_mask);
        body.push_back(kI64And);
    }
    emit_local(body, kLocalSet, kTmpB); // t_b = src
    emit_local(body, kLocalGet, kTmpA);
    emit_local(body, kLocalGet, kTmpB);
    body.push_back(opcode);
    // cmp/test keep the result in t_c: the destination register is not
    // written, only the flags chain consumes it.
    const std::uint32_t result = cmp_test ? kTmpC : *dst;
    emit_local(body, kLocalSet, result);
    if (!qword) {
        // dword stores zero-extend; t_a/t_b stay masked so the flag chain
        // (cf via i64.lt_u, zf via eqz) matches 32-bit semantics.
        emit_local(body, kLocalGet, result);
        emit_i64(body, width_mask);
        body.push_back(kI64And);
        emit_local(body, kLocalSet, result);
    }
    emit_flags(body, result, alu_kind(insn.mnemonic), qword, base);
    return true;
}

/// Cc of the terminating jcc of a lowered prefix of @p insns.
arch::x86_64::Cc last_cc(std::span<const arch::x86_64::DecodedInsn> insns, std::size_t lowered) {
    return insns[lowered - 1].cc;
}

} // namespace

WasmBlockInfo append_wasm_block(std::span<const arch::x86_64::DecodedInsn> insns, uint64_t base_rip,
                                std::vector<std::uint8_t>& body, std::uint32_t gpr_base,
                                std::uint32_t ram_limit) {
    // Pass 1: lower into locals and collect liveness. The body starts with
    // the entry loads (emitted last, once the used set is known); the exit
    // stores and the next-rip return follow the instruction stream.
    WasmBlockInfo info;
    std::vector<std::uint8_t> code;
    RegMask mask;
    uint64_t next_rip = base_rip;
    unsigned done = 0; // instructions completed before the current one
    TermExit term;     // survives the loop: the exit switch reads the step rip
    for (const auto& insn : insns) {
        term = TermExit{};
        const std::size_t before = code.size();
        if (!lower_insn(insn, next_rip, code, term, gpr_base, ram_limit, done, mask.mask_dirty())) {
            code.resize(before); // half-emitted unsupported form
            break;
        }
        if (insn.mnemonic != Mnemonic::kRet && insn.mnemonic != Mnemonic::kJmp &&
            insn.mnemonic != Mnemonic::kJcc) {
            for (const auto& op : insn.ops) {
                if (op.kind == Operand::Kind::kReg) {
                    const auto local = local_of(op.reg);
                    // qword and dword both bridge into the full 64-bit slot
                    if (local.has_value() && (op.size == 8 || op.size == 4)) {
                        mask.mark_used(*local);
                    }
                }
                if (op.kind == Operand::Kind::kMem) {
                    if (op.has_base) {
                        const auto local = local_of(op.base);
                        if (local.has_value()) {
                            mask.mark_used(*local);
                        }
                    }
                    if (op.has_index) {
                        const auto local = local_of(op.index);
                        if (local.has_value()) {
                            mask.mark_used(*local);
                        }
                    }
                }
            }
            if (writes_dst(insn.mnemonic) && insn.ops[0].kind == Operand::Kind::kReg) {
                const auto dst = local_of(insn.ops[0].reg);
                if (dst.has_value()) {
                    mask.mark_dirty(*dst);
                }
            }
        }
        if (writes_flags(insn.mnemonic)) {
            info.flags_used = true; // the chain reads t_a/t_b and the cell
            info.flags_dirty = true;
        }
        ++info.lowered;
        ++done;
        next_rip += insn.length;
        if (term.kind != WasmTerm::kNext) {
            info.term = term.kind;
            info.jump_target = term.taken;
            info.fall_rip = term.fall;
            break;
        }
    }
    if (info.lowered == 0) {
        return info;
    }
    info.used_mask = mask.mask_used();
    info.dirty_mask = mask.mask_dirty();
    // clear the handoff flag: a stale 1 would fool the runner after a
    // previous block of this module handed control back
    emit_i32(body, static_cast<std::int32_t>(detail::kHandedSlot + gpr_base));
    emit_i32(body, 0);
    body.push_back(detail::kI32Store);
    body.push_back(detail::kAlign4);
    append_uleb(body, 0);
    emit_i32(body, static_cast<std::int32_t>(detail::kStoreCountSlot + gpr_base));
    emit_i32(body, 0);
    body.push_back(detail::kI32Store);
    body.push_back(detail::kAlign4);
    append_uleb(body, 0);
    // Entry bridge: load every used GPR slot into its local.
    for (std::uint32_t local = 0; local < kNumGprs; ++local) {
        if (mask.used_ok(local)) {
            emit_i32(body, static_cast<std::int32_t>(local * 8U));
            emit_load(body, gpr_base);
            emit_local(body, kLocalSet, local);
        }
    }
    body.insert(body.end(), code.begin(), code.end());
    // Exit bridge: store dirtied locals back, then report the next rip.
    for (std::uint32_t local = 0; local < kNumGprs; ++local) {
        if (mask.dirty_ok(local)) {
            emit_i32(body, static_cast<std::int32_t>(local * 8U));
            emit_local(body, kLocalGet, local);
            emit_store(body, gpr_base);
        }
    }
    switch (info.term) {
    case WasmTerm::kNext:
        emit_i64(body, static_cast<std::int64_t>(next_rip));
        break;
    case WasmTerm::kStep:
        // ret cannot resolve its target without a stack read: hand the
        // ret rip back (flagged in the mirror) for one interpreter step.
        detail::emit_handoff(body, gpr_base, info.lowered - 1, term.taken, 0);
        break;
    case WasmTerm::kJump:
        emit_i64(body, static_cast<std::int64_t>(info.jump_target));
        break;
    case WasmTerm::kBranch:
        // taken, fallthrough, cond on the stack: select returns the
        // actually-taken rip, the caller stores it and keeps running.
        emit_i64(body, static_cast<std::int64_t>(info.jump_target));
        emit_i64(body, static_cast<std::int64_t>(info.fall_rip));
        info.flags_used = true;
        emit_cc(body, last_cc(insns, info.lowered), gpr_base);
        body.push_back(kSelect);
        break;
    }
    body.push_back(kReturn);
    return info;
}

std::size_t wasm_block_flags_slot() {
    return kFlagsSlot;
}

} // namespace micro_forge::base
