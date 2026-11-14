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
#include "micro_forge/base/wasm_block_internal.hpp"
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

/// Two-register form (ops[0] and ops[1] both GPR slots, one width).
bool reg_reg(const arch::x86_64::DecodedInsn& insn) {
    return reglike(insn.ops[0]) && insn.ops[1].kind == Operand::Kind::kReg &&
           insn.ops[0].size != 0 && insn.ops[0].size <= 8 && insn.ops[1].size == insn.ops[0].size;
}

/// Register/immediate form (ops[0] GPR slot, ops[1] kImm).
bool reg_imm(const arch::x86_64::DecodedInsn& insn) {
    return reglike(insn.ops[0]) && insn.ops[1].kind == Operand::Kind::kImm &&
           insn.ops[0].size != 0 && insn.ops[0].size <= 8;
}

/// Zeros the carry-in local (reg-path ALU never has an x86 carry-in).
void emit_carry_in_zero(std::vector<std::uint8_t>& body, std::uint32_t /*base*/) {
    emit_i64(body, 0);
    emit_local(body, kLocalSet, kTmpCi);
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
    // every lowering that stores a result into ops[0] belongs here: a
    // missing entry leaves the local out of the dirty set and the block
    // exit never writes it back to the mirror.
    return mnemonic == Mnemonic::kMov || mnemonic == Mnemonic::kSetcc ||
           mnemonic == Mnemonic::kLea || alu_opcode(mnemonic) != 0;
}

/// Mnemonics whose lowering writes the flags cell or reads CF (bt).
bool writes_flags(Mnemonic mnemonic) {
    return alu_opcode(mnemonic) != 0 || mnemonic == Mnemonic::kCmp || mnemonic == Mnemonic::kTest ||
           mnemonic == Mnemonic::kBt;
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
        // Pop the return address inside the block: read [rsp] through the
        // same walk the data instructions use, then retire the frame. A
        // refusal leaves rsp untouched and hands the ret to the interpreter.
        const auto rsp_local = local_of(arch::x86_64::Reg::kRsp);
        if (!rsp_local.has_value()) {
            return false;
        }
        const Handoff ho{.base = base, .done = done, .rip = rip, .dirty_bits = dirty_so_far};
        emit_local(body, kLocalGet, *rsp_local);
        emit_local(body, kLocalSet, kTmpV);
        emit_walk(body, 8, ram_limit, ho, /*write=*/false);
        emit_guest_load(body, 8);
        emit_local(body, kLocalSet, kTmpC); // popped return address
        emit_local(body, kLocalGet, *rsp_local);
        emit_i64(body, 8);
        body.push_back(kI64Add);
        emit_local(body, kLocalSet, *rsp_local);
        term.kind = WasmTerm::kIndirect;
        return true;
    }
    if (insn.mnemonic == Mnemonic::kJmp && insn.ops[0].kind == Operand::Kind::kImm) {
        term.kind = WasmTerm::kJump;
        term.taken = after + static_cast<std::uint64_t>(insn.ops[0].disp);
        return true;
    }
    if (insn.mnemonic == Mnemonic::kJcc && insn.ops[0].kind == Operand::Kind::kImm) {
        // The fall path continues inside this block; the taken side exits
        // here and reports how many instructions actually ran.
        const std::uint64_t taken = after + static_cast<std::uint64_t>(insn.ops[0].disp);
        emit_cc(body, insn.cc, base);
        emit_if_void(body);
        emit_branch_exit(
            body,
            Handoff{.base = base, .done = done + 1U, .rip = taken, .dirty_bits = dirty_so_far});
        body.push_back(kBlockEnd);
        return true;
    }
    // setcc r8: merge the 0/1 condition byte into the register's high bits
    if (insn.mnemonic == Mnemonic::kSetcc && reglike(insn.ops[0]) && insn.ops[0].size == 1) {
        const auto dst = local_of(insn.ops[0].reg);
        if (!dst.has_value() || !mirror_ok(insn, insn.ops[0])) {
            return false;
        }
        emit_cc(body, insn.cc, base);
        // emit_cc yields the raw masked flag bits; normalize to 0/1
        body.push_back(kI32Eqz);
        body.push_back(kI32Eqz);
        body.push_back(kI64ExtendI32U);
        emit_local(body, kLocalSet, kTmpC);
        emit_dst_write(body, *dst, kTmpC, 1);
        return true;
    }
    // lea r, [m]: effective address only, no guest access, flags untouched
    if (insn.mnemonic == Mnemonic::kLea && reglike(insn.ops[0]) &&
        insn.ops[1].kind == Operand::Kind::kMem) {
        const auto dst = local_of(insn.ops[0].reg);
        if (!dst.has_value() || !mirror_ok(insn, insn.ops[0])) {
            return false;
        }
        const auto& mem = insn.ops[1];
        if (mem.rip_relative) {
            // the address is a compile-time constant: instruction end + disp
            emit_i64(body, static_cast<std::int64_t>(rip + insn.length) +
                               static_cast<std::int64_t>(mem.disp));
            emit_local(body, kLocalSet, kTmpC);
        } else {
            if (!emit_vaddr(mem, body)) {
                return false;
            }
            emit_local(body, kLocalGet, kTmpV);
            emit_local(body, kLocalSet, kTmpC);
        }
        emit_dst_write(body, *dst, kTmpC, insn.ops[0].size);
        return true;
    }
    if (insn.ops[0].kind == Operand::Kind::kMem || insn.ops[1].kind == Operand::Kind::kMem) {
        return lower_mem_insn(insn, rip, body, base, ram_limit, done, dirty_so_far);
    }
    const unsigned size = insn.ops[0].size;
    const std::int64_t width_mask = width_mask_of(size);
    const bool cmp_test = insn.mnemonic == Mnemonic::kCmp || insn.mnemonic == Mnemonic::kTest;
    const bool imm_src = reg_imm(insn);
    const bool reg_src = reg_reg(insn);
    if ((!imm_src && !reg_src) ||
        (!cmp_test && insn.mnemonic != Mnemonic::kMov &&
         (!writes_flags(insn.mnemonic) || insn.mnemonic == Mnemonic::kBt))) {
        // bt has no ALU opcode here (alu_opcode returns 0, which is the
        // wasm unreachable opcode) — only the dedicated r,r path above
        // lowers it; every other bt shape must fall back to the interpreter
        return false;
    }
    const auto dst = local_of(insn.ops[0].reg);
    const auto src = imm_src ? std::optional<std::uint32_t>{} : local_of(insn.ops[1].reg);
    if (!dst.has_value() || !mirror_ok(insn, insn.ops[0]) ||
        (!imm_src && (!src.has_value() || !mirror_ok(insn, insn.ops[1])))) {
        return false;
    }
    if (insn.mnemonic == Mnemonic::kMov) {
        if (imm_src) {
            // NOLINTNEXTLINE(bugprone-signed-bitwise) -- imm zero-extend mask
            emit_i64(body, insn.ops[1].disp & width_mask); // imm loads zero-extend
            emit_local(body, kLocalSet, kTmpC);
        } else {
            emit_local(body, kLocalGet, *src);
            if (size < 8) {
                emit_i64(body, width_mask);
                body.push_back(kI64And);
            }
            emit_local(body, kLocalSet, kTmpC);
        }
        emit_dst_write(body, *dst, kTmpC, size);
        return true;
    }
    const std::uint8_t opcode = cmp_alu_opcode(insn.mnemonic);
    emit_local(body, kLocalGet, *dst);
    if (size < 8) {
        emit_i64(body, width_mask);
        body.push_back(kI64And); // t_a must match the operand domain too
    }
    emit_local(body, kLocalSet, kTmpA); // t_a = dst
    if (imm_src) {
        // NOLINTNEXTLINE(bugprone-signed-bitwise) -- operand mask
        emit_i64(body, insn.ops[1].disp & width_mask);
    } else {
        emit_local(body, kLocalGet, *src);
        if (size < 8) {
            emit_i64(body, width_mask);
            body.push_back(kI64And);
        }
    }
    emit_local(body, kLocalSet, kTmpB); // t_b = src
    emit_carry_in_zero(body, base);
    emit_local(body, kLocalGet, kTmpA);
    emit_local(body, kLocalGet, kTmpB);
    body.push_back(opcode);
    emit_local(body, kLocalSet, kTmpC);
    if (size < 8) {
        // t_c stays masked so the flag chain (carry via s2, zf via eqz)
        // matches sub-dword semantics; the dst write merges after.
        emit_local(body, kLocalGet, kTmpC);
        emit_i64(body, width_mask);
        body.push_back(kI64And);
        emit_local(body, kLocalSet, kTmpC);
    }
    emit_flags(body, kTmpC, alu_kind(insn.mnemonic), top_bit_of(size), base);
    if (!cmp_test) {
        emit_dst_write(body, *dst, kTmpC, size);
    }
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
            // pack mnemonic + operand shape so the harness can histogram
            // exactly which form stopped the slice
            info.stopped_at = static_cast<std::uint32_t>(insn.mnemonic) |
                              (static_cast<std::uint32_t>(insn.ops[0].kind) << 8U) |
                              (static_cast<std::uint32_t>(insn.ops[1].kind) << 12U) |
                              (static_cast<std::uint32_t>(insn.ops[0].size) << 16U);
            break;
        }
        if (insn.mnemonic != Mnemonic::kRet && insn.mnemonic != Mnemonic::kJmp &&
            insn.mnemonic != Mnemonic::kJcc) {
            for (const auto& op : insn.ops) {
                if (reglike(op) && op.size != 0 && op.size <= 8 && mirror_ok(insn, op)) {
                    // every width bridges into the full 64-bit slot; byte
                    // and word writes also read the old value for the merge
                    const auto local = local_of(op.reg);
                    if (local.has_value()) {
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
            if (writes_dst(insn.mnemonic) && reglike(insn.ops[0])) {
                const auto dst = local_of(insn.ops[0].reg);
                if (dst.has_value()) {
                    mask.mark_dirty(*dst);
                }
            }
        }
        if (insn.mnemonic == Mnemonic::kRet) {
            const auto rsp_local = local_of(arch::x86_64::Reg::kRsp);
            if (rsp_local.has_value()) {
                mask.mark_used(*rsp_local);
                mask.mark_dirty(*rsp_local);
            }
        }
        if (insn.mnemonic == Mnemonic::kCall && insn.ops[0].kind == Operand::Kind::kImm) {
            const auto rsp_local = local_of(arch::x86_64::Reg::kRsp);
            if (rsp_local.has_value()) {
                mask.mark_used(*rsp_local);
                mask.mark_dirty(*rsp_local);
            }
        }
        if (insn.mnemonic == Mnemonic::kJcc) {
            info.flags_used = true;
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
    // Permission state is block-invariant: resolve once, use per access.
    emit_i32(body, static_cast<std::int32_t>(detail::kCplSlot + gpr_base));
    body.push_back(detail::kI32Load8U);
    body.push_back(detail::kAlign0);
    append_uleb(body, 0);
    emit_local(body, detail::kLocalSet, detail::kPermCpl);
    emit_i32(body, static_cast<std::int32_t>(detail::kCr0Slot + gpr_base));
    body.push_back(detail::kI64Load);
    body.push_back(detail::kAlign8);
    append_uleb(body, 0);
    emit_i64(body, 16);
    body.push_back(detail::kI64ShrU);
    body.push_back(detail::kI32Wrap);
    emit_i32(body, 1);
    body.push_back(detail::kI32And);
    emit_local(body, detail::kLocalSet, detail::kPermWp);
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
    case WasmTerm::kIndirect:
        emit_local(body, kLocalGet, kTmpC); // popped return address
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
    // Record how many instructions the block actually retired: the runner
    // reads this instead of assuming the whole slice ran.
    emit_i32(body, static_cast<std::int32_t>(detail::kExitDoneSlot + gpr_base));
    emit_i32(body, static_cast<std::int32_t>(info.lowered));
    body.push_back(detail::kI32Store);
    body.push_back(detail::kAlign4);
    append_uleb(body, 0);
    body.push_back(kReturn);
    return info;
}

std::size_t wasm_block_flags_slot() {
    return kFlagsSlot;
}

} // namespace micro_forge::base
