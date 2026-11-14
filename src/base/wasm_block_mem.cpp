/**
 * @file wasm_block_mem.cpp
 * @brief Memory-operand lowering for wasm blocks (inline page-table walk).
 * @version 0.1.0
 *
 * Guest data accesses compile to a four-level page walk over the engine
 * linear memory (page tables live in guest RAM, which is a direct window).
 * Any walk failure, non-kernel mode, WP=1 or an out-of-RAM physical address
 * returns the step sentinel so the runner takes one interpreter step and
 * raises the fault (or succeeds) through the one true implementation.
 */

#include "micro_forge/arch/x86_64/decoded.hpp"
#include "micro_forge/base/wasm_block_detail.hpp"
#include "micro_forge/base/wasm_block_internal.hpp"
#include "micro_forge/base/wasm_emit.hpp"

#include <cstdint>
#include <vector>

namespace micro_forge::base::detail {

namespace {

using arch::x86_64::DecodedInsn;
using arch::x86_64::Mnemonic;
using arch::x86_64::Operand;
using arch::x86_64::Seg;

} // namespace

/// Effective-address computation of @p op into kTmpV (base+index*scale+disp).
bool emit_vaddr(const Operand& op, std::vector<std::uint8_t>& body) {
    if (op.rip_relative) {
        return false;
    }
    if (op.has_base) {
        const auto base = local_of(op.base);
        if (!base.has_value()) {
            return false;
        }
        emit_local(body, kLocalGet, *base);
    } else {
        emit_i64(body, 0);
    }
    if (op.has_index) {
        const auto index = local_of(op.index);
        if (!index.has_value()) {
            return false;
        }
        emit_local(body, kLocalGet, *index);
        unsigned shift = 0;
        for (unsigned s = op.scale; s > 1U; s >>= 1U) {
            ++shift;
        }
        if ((1U << shift) != op.scale) {
            return false; // only power-of-two scales exist in x86 anyway
        }
        emit_i64(body, static_cast<std::int64_t>(shift));
        body.push_back(kI64ShlOp);
        body.push_back(kI64Add);
    }
    if (op.disp != 0) {
        emit_i64(body, op.disp);
        body.push_back(kI64Add);
    }
    emit_local(body, kLocalSet, kTmpV);
    return true;
}

/// Zero-extending guest load of @p size bytes at the address on the stack.
void emit_guest_load(std::vector<std::uint8_t>& body, unsigned size) {
    bool extend = false;
    switch (size) {
    case 8:
        body.push_back(kI64Load);
        body.push_back(kAlign8);
        break;
    case 4:
        body.push_back(kI32Load);
        body.push_back(kAlign4);
        extend = true;
        break;
    case 2:
        body.push_back(kI32Load16U);
        body.push_back(kAlign1);
        extend = true;
        break;
    default:
        body.push_back(kI32Load8U);
        body.push_back(kAlign0);
        extend = true;
        break;
    }
    append_uleb(body, 0); // memarg offset 0: address already on the stack
    if (extend) {
        body.push_back(kI64ExtendI32U);
    }
}

/// Appends kTmpPd to the mirror store-log so the runner can invalidate the
/// interpreter decode cache (self-modifying code) after the block returns.
void emit_store_log(std::vector<std::uint8_t>& body, std::uint32_t base) {
    emit_i32(body, static_cast<std::int32_t>(kStoreCountSlot + base));
    body.push_back(kI32Load);
    body.push_back(kAlign4);
    append_uleb(body, 0);
    emit_local(body, kLocalSet, kTmpP);
    emit_local(body, kLocalGet, kTmpP);
    emit_i32(body, 8);
    body.push_back(kI32LtU);
    emit_if_void(body);
    emit_i32(body, static_cast<std::int32_t>(kStoreLogSlot + base));
    emit_local(body, kLocalGet, kTmpP);
    emit_i32(body, 3);
    body.push_back(kI32Shl);
    body.push_back(kI32Add);
    emit_local(body, kLocalGet, kTmpPd);
    body.push_back(kI64Store);
    body.push_back(kAlign8);
    append_uleb(body, 0);
    emit_i32(body, static_cast<std::int32_t>(kStoreCountSlot + base));
    emit_local(body, kLocalGet, kTmpP);
    emit_i32(body, 1);
    body.push_back(kI32Add);
    body.push_back(kI32Store);
    body.push_back(kAlign4);
    append_uleb(body, 0);
    body.push_back(kBlockEnd);
}

/// Guest store of @p size bytes; the address and the i64 value are on the
/// stack (address first).
void emit_guest_store(std::vector<std::uint8_t>& body, unsigned size) {
    if (size < 8) {
        body.push_back(kI32Wrap);
    }
    switch (size) {
    case 8:
        body.push_back(kI64Store);
        body.push_back(kAlign8);
        break;
    case 4:
        body.push_back(kI32Store);
        body.push_back(kAlign4);
        break;
    case 2:
        body.push_back(kI32Store16);
        body.push_back(kAlign1);
        break;
    default:
        body.push_back(kI32Store8);
        body.push_back(kAlign0);
        break;
    }
    append_uleb(body, 0);
}

namespace {} // namespace

void emit_handoff(std::vector<std::uint8_t>& body, std::uint32_t base, unsigned done,
                  std::uint64_t rip, std::uint32_t dirty_bits) {
    emit_handoff(body, Handoff{.base = base, .done = done, .rip = rip, .dirty_bits = dirty_bits});
}

void emit_flags(std::vector<std::uint8_t>& body, std::uint32_t dst, unsigned kind, unsigned top_bit,
                std::uint32_t base) {
    const auto top = static_cast<std::int32_t>(top_bit);
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
    emit_i32(body, static_cast<std::int32_t>(kFlagsSlot + base));
    // value = cf (fold every further bit in with shl/or)
    if (kind == 2) {
        emit_i32(body, 0); // logic ops clear cf
    } else if (kind == 0) {
        // add: s1 = a+b, s2 = s1+ci; width 8 carry is carry1|carry2, below
        // width 8 it is the shift-out bit of s2 (interpreter alu_add)
        emit_local(body, kLocalGet, kTmpA);
        emit_local(body, kLocalGet, kTmpB);
        body.push_back(kI64Add);
        emit_local(body, kLocalSet, kTmpE); // s1
        emit_local(body, kLocalGet, kTmpE);
        emit_local(body, kLocalGet, kTmpCi);
        body.push_back(kI64Add);
        emit_local(body, kLocalSet, kTmpV); // s2
        if (top_bit == 63U) {
            emit_local(body, kLocalGet, kTmpE);
            emit_local(body, kLocalGet, kTmpA);
            body.push_back(kI64LtU); // carry1
            emit_local(body, kLocalGet, kTmpV);
            emit_local(body, kLocalGet, kTmpE);
            body.push_back(kI64LtU); // carry2
            body.push_back(kI32Or);
        } else {
            emit_local(body, kLocalGet, kTmpV);
            emit_i64(body, top + 1);
            body.push_back(kI64ShrU);
            body.push_back(kI32Wrap);
            emit_i32(body, 1);
            body.push_back(kI32And);
        }
    } else {
        // sub: borrow1 = a<b, borrow2 = (a-b) < ci (interpreter alu_sub)
        emit_local(body, kLocalGet, kTmpA);
        emit_local(body, kLocalGet, kTmpB);
        body.push_back(kI64Sub);
        emit_local(body, kLocalSet, kTmpE); // diff1
        emit_local(body, kLocalGet, kTmpA);
        emit_local(body, kLocalGet, kTmpB);
        body.push_back(kI64LtU); // borrow1
        emit_local(body, kLocalGet, kTmpE);
        emit_local(body, kLocalGet, kTmpCi);
        body.push_back(kI64LtU); // borrow2
        body.push_back(kI32Or);
    }
    // zf = res == 0 (i64.eqz)
    emit_local(body, kLocalGet, dst);
    body.push_back(kI64Eqz);
    emit_i32(body, 1);
    body.push_back(kI32Shl);
    body.push_back(kI32Or);
    // sf = (res >> top) & 1
    emit_local(body, kLocalGet, dst);
    emit_i64(body, top);
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
        if (kind == 0) { // add: (~(a^b) & (a^res)) >> top
            emit_i64(body, -1);
            body.push_back(kI64Xor);
        }
        emit_local(body, kLocalGet, kTmpA);
        emit_local(body, kLocalGet, dst);
        body.push_back(kI64Xor);
        body.push_back(kI64And);
        emit_i64(body, top);
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
    // af (bit 5): the interpreter computes it per family and the kernel
    // state depends on that exact choice — add: bit 4 of a^b^sum, sub/cmp:
    // bit 3 of a^b^diff, logic: cleared. Mirror alu_add/alu_sub/alu_logic.
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
    append_uleb(body, 0);
}

/**
 * @brief Emits the carry-in local for an ALU flag chain.
 *
 * adc/sbb read CF from the flags cell; plain add/sub/cmp take a hard 0.
 */
namespace {
void emit_carry_in(std::vector<std::uint8_t>& body, bool from_flags, std::uint32_t base) {
    if (from_flags) {
        emit_i32(body, static_cast<std::int32_t>(kFlagsSlot + base));
        body.push_back(kI32Load);
        body.push_back(kAlign4);
        append_uleb(body, 0);
        emit_i32(body, 1);
        body.push_back(kI32And);
        body.push_back(kI64ExtendI32U);
    } else {
        emit_i64(body, 0);
    }
    emit_local(body, kLocalSet, kTmpCi);
}
} // namespace

namespace {
/// Loads [m], ALU with the register source, store back ([m],r RMW family).
bool lower_memdst_alu(const DecodedInsn& insn, const Operand& mem, const Operand& src_op,
                      std::vector<std::uint8_t>& body, std::uint32_t base, std::uint32_t ram_limit,
                      unsigned done, std::uint64_t rip, std::uint32_t dirty_bits) {
    unsigned kind = 2;
    std::uint8_t opcode = 0;
    bool carry = false;
    const bool cmp_test = insn.mnemonic == Mnemonic::kCmp || insn.mnemonic == Mnemonic::kTest;
    switch (insn.mnemonic) {
    case Mnemonic::kAdc:
        carry = true;
        opcode = kI64Add;
        kind = 0;
        break;
    case Mnemonic::kSbb:
        carry = true;
        opcode = kI64Sub;
        kind = 1;
        break;
    case Mnemonic::kCmp:
        opcode = kI64Sub;
        kind = 1;
        break;
    case Mnemonic::kTest:
        opcode = kI64And;
        kind = 2;
        break;
    case Mnemonic::kAdd:
        opcode = kI64Add;
        kind = 0;
        break;
    case Mnemonic::kSub:
        opcode = kI64Sub;
        kind = 1;
        break;
    case Mnemonic::kAnd:
        opcode = kI64And;
        kind = 2;
        break;
    case Mnemonic::kOr:
        opcode = kI64Or;
        kind = 2;
        break;
    case Mnemonic::kXor:
        opcode = kI64Xor;
        kind = 2;
        break;
    default:
        return false;
    }
    const unsigned size = src_op.size;
    const auto src = local_of(src_op.reg);
    if (!src.has_value() || size == 0 || size > 8 || !mirror_ok(insn, src_op)) {
        return false;
    }
    const std::int64_t mask = width_mask_of(size);
    // x86 evaluates [mem] OP reg as memory op register, and sub/cmp/sbb
    // are not commutative: operand A must be the loaded memory value.
    // t_b = register source masked to the operand width
    emit_local(body, kLocalGet, *src);
    if (size < 8) {
        emit_i64(body, mask);
        body.push_back(kI64And);
    }
    emit_local(body, kLocalSet, kTmpB);
    // t_a = loaded memory operand (zero-extended by the load)
    if (!emit_vaddr(mem, body)) {
        return false;
    }
    const Handoff ho{.base = base, .done = done, .rip = rip, .dirty_bits = dirty_bits};
    emit_walk(body, mem.size, ram_limit, ho, /*write=*/false);
    emit_guest_load(body, mem.size);
    emit_local(body, kLocalSet, kTmpA);
    emit_carry_in(body, carry, base);
    // t_c = t_a op t_b (+/- carry-in for adc/sbb)
    emit_local(body, kLocalGet, kTmpA);
    emit_local(body, kLocalGet, kTmpB);
    body.push_back(opcode);
    emit_local(body, kLocalSet, kTmpC);
    if (carry) {
        emit_local(body, kLocalGet, kTmpC);
        emit_local(body, kLocalGet, kTmpCi);
        body.push_back(insn.mnemonic == Mnemonic::kAdc ? kI64Add : kI64Sub);
        emit_local(body, kLocalSet, kTmpC);
    }
    if (size < 8) {
        emit_local(body, kLocalGet, kTmpC);
        emit_i64(body, mask);
        body.push_back(kI64And);
        emit_local(body, kLocalSet, kTmpC);
    }
    if (!cmp_test) {
        // store back: recompute the address, the walk locals are scratch
        if (!emit_vaddr(mem, body)) {
            return false;
        }
        emit_walk(body, mem.size, ram_limit, ho, /*write=*/true);
        emit_local(body, kLocalGet, kTmpC);
        emit_guest_store(body, mem.size);
        emit_store_log(body, base);
    }
    emit_flags(body, kTmpC, kind, top_bit_of(size), base);
    return true;
}
} // namespace

bool lower_mem_insn(const DecodedInsn& insn, std::uint64_t rip, std::vector<std::uint8_t>& body,
                    std::uint32_t base, std::uint32_t ram_limit, unsigned done,
                    std::uint32_t dirty_bits) {
    if (insn.seg == Seg::kFs || insn.seg == Seg::kGs) {
        return false; // fs/gs per-cpu bases belong to the interpreter
    }
    int mem_idx = -1;
    if (insn.ops[0].kind == Operand::Kind::kMem) {
        mem_idx = 0;
    } else if (insn.ops[1].kind == Operand::Kind::kMem) {
        mem_idx = 1;
    }
    if (mem_idx < 0) {
        return false;
    }
    const Operand& mem = insn.ops[mem_idx];
    const Operand& other = insn.ops[mem_idx == 0 ? 1 : 0];
    const bool qword = other.size == 8;
    const std::int64_t width_mask =
        qword ? ~INT64_C(0) : static_cast<std::int64_t>(UINT64_C(0xFFFFFFFF));

    if (insn.mnemonic == Mnemonic::kMov && mem_idx == 0) {
        // mov [m], reg / mov [m], imm (8/16/32/64 stores)
        if (other.kind != Operand::Kind::kReg && other.kind != Operand::Kind::kImm) {
            return false;
        }
        if (other.kind == Operand::Kind::kReg && !mirror_ok(insn, other)) {
            return false; // un-REXed h-reg byte stores stay with the interpreter
        }
        if (!emit_vaddr(mem, body)) {
            return false;
        }
        const Handoff ho{.base = base, .done = done, .rip = rip, .dirty_bits = dirty_bits};
        emit_walk(body, mem.size, ram_limit, ho, /*write=*/true);
        if (other.kind == Operand::Kind::kImm) {
            emit_i64(body, other.disp);
        } else {
            const auto src = local_of(other.reg);
            if (!src.has_value()) {
                return false;
            }
            emit_local(body, kLocalGet, *src);
            if (!qword && other.size == 8) {
                emit_i64(body, width_mask);
                body.push_back(kI64And);
            }
        }
        emit_guest_store(body, mem.size);
        emit_store_log(body, base);
        return true;
    }

    // Memory-destination RMW ALU ([m],r under lock too: single-threaded
    // guest, load-op-store with the store log on the interpreter's side).
    if (mem_idx == 0) {
        if (other.kind == Operand::Kind::kReg) {
            return lower_memdst_alu(insn, mem, other, body, base, ram_limit, done, rip, dirty_bits);
        }
        return false; // xchg [m],r / shl [m],i / neg [m] ... stay interpreted
    }
    // Everything else needs a GPR counterpart (al-form implicit included).
    if (!reglike(other) || other.size == 0 || other.size > 8 || !mirror_ok(insn, other)) {
        return false;
    }
    const auto dst = local_of(other.reg);
    if (!dst.has_value()) {
        return false;
    }
    if (!emit_vaddr(mem, body)) {
        return false;
    }

    const Handoff ho{.base = base, .done = done, .rip = rip, .dirty_bits = dirty_bits};
    // guard already ran at block entry (cpl/CR0.WP are block-invariant)
    emit_walk(body, mem.size, ram_limit, ho, /*write=*/false);
    emit_guest_load(body, mem.size);
    emit_local(body, kLocalSet, kTmpB); // t_b = loaded source (zero-extended)
    emit_local(body, kLocalGet, *dst);
    if (!qword) {
        emit_i64(body, width_mask);
        body.push_back(kI64And);
    }
    emit_local(body, kLocalSet, kTmpA);
    if (insn.mnemonic == Mnemonic::kMov) { // mov reg, [m] (8/16 merge)
        emit_dst_write(body, *dst, kTmpB, other.size);
        return true;
    }
    std::uint8_t opcode = 0;
    unsigned kind = 2;
    bool carry = false;
    const bool cmp_test = insn.mnemonic == Mnemonic::kCmp || insn.mnemonic == Mnemonic::kTest;
    switch (insn.mnemonic) {
    case Mnemonic::kAdc:
        carry = true;
        opcode = kI64Add;
        kind = 0;
        break;
    case Mnemonic::kSbb:
        carry = true;
        opcode = kI64Sub;
        kind = 1;
        break;
    case Mnemonic::kCmp:
        opcode = kI64Sub;
        kind = 1;
        break;
    case Mnemonic::kTest:
        opcode = kI64And;
        kind = 2;
        break;
    case Mnemonic::kAdd:
        opcode = kI64Add;
        kind = 0;
        break;
    case Mnemonic::kSub:
        opcode = kI64Sub;
        kind = 1;
        break;
    case Mnemonic::kAnd:
        opcode = kI64And;
        kind = 2;
        break;
    case Mnemonic::kOr:
        opcode = kI64Or;
        kind = 2;
        break;
    case Mnemonic::kXor:
        opcode = kI64Xor;
        kind = 2;
        break;
    default:
        return false;
    }
    emit_carry_in(body, carry, base);
    emit_local(body, kLocalGet, kTmpA);
    emit_local(body, kLocalGet, kTmpB);
    body.push_back(opcode);
    emit_local(body, kLocalSet, kTmpC);
    if (carry) {
        emit_local(body, kLocalGet, kTmpC);
        emit_local(body, kLocalGet, kTmpCi);
        body.push_back(insn.mnemonic == Mnemonic::kAdc ? kI64Add : kI64Sub);
        emit_local(body, kLocalSet, kTmpC);
    }
    if (other.size < 8) {
        emit_local(body, kLocalGet, kTmpC);
        emit_i64(body, width_mask_of(other.size));
        body.push_back(kI64And);
        emit_local(body, kLocalSet, kTmpC);
    }
    emit_flags(body, kTmpC, kind, top_bit_of(other.size), base);
    if (!cmp_test) {
        emit_dst_write(body, *dst, kTmpC, other.size);
    }
    return true;
}

} // namespace micro_forge::base::detail
