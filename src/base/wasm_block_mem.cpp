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

#include "micro_forge/base/wasm_block_detail.hpp"

#include <cstdint>
#include <vector>

namespace micro_forge::base::detail {

namespace {

using arch::x86_64::DecodedInsn;
using arch::x86_64::Mnemonic;
using arch::x86_64::Operand;
using arch::x86_64::Seg;

/// Facts a mid-block refusal needs to hand the rest back to the runner.
struct Handoff {
    std::uint32_t base;   ///< GPR mirror base offset.
    unsigned done;        ///< instructions of this block already complete.
    std::uint64_t rip;    ///< guest rip the interpreter resumes from.
    std::uint32_t dirty_bits; ///< locals written so far that must reach the mirror.
};

/// Marks the mirror flag, records the done count, returns @p ho.rip.
void emit_handoff(std::vector<std::uint8_t>& body, const Handoff& ho) {
    // the early return skips the exit bridge: push everything the block
    // has already written back to the mirror before handing control over
    for (std::uint32_t local = 0; local < kNumGprs; ++local) {
        if ((ho.dirty_bits >> local) & 1U) {
            emit_i32(body, static_cast<std::int32_t>(local * 8U));
            emit_local(body, kLocalGet, local);
            emit_store(body, ho.base);
        }
    }
    emit_i32(body, static_cast<std::int32_t>(kHandoffDirtySlot + ho.base));
    emit_i32(body, static_cast<std::int32_t>(ho.dirty_bits));
    body.push_back(kI32Store);
    body.push_back(kAlign4);
    append_uleb(body, 0);
    emit_i32(body, static_cast<std::int32_t>(kHandedSlot + ho.base));
    emit_i32(body, 1);
    body.push_back(kI32Store);
    body.push_back(kAlign4);
    append_uleb(body, 0);
    emit_i32(body, static_cast<std::int32_t>(kDoneSlot + ho.base));
    emit_i32(body, static_cast<std::int32_t>(ho.done));
    body.push_back(kI32Store);
    body.push_back(kAlign4);
    append_uleb(body, 0);
    emit_i64(body, static_cast<std::int64_t>(ho.rip));
    body.push_back(kReturn);
}


/// void if whose condition is already on the stack.
void emit_if_void(std::vector<std::uint8_t>& body) {
    body.push_back(kIfVoid);
    body.push_back(0x40); // no result
}

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

/// Kernel-mode guard: cpl != 0 or CR0.WP=1 returns the sentinel (the
/// interpreter re-checks user/write permissions we cannot mirror cheaply).
void emit_mode_guard(std::vector<std::uint8_t>& body, const Handoff& ho) {
    emit_i32(body, static_cast<std::int32_t>(kCplSlot + ho.base));
    body.push_back(kI32Load8U);
    body.push_back(kAlign0);
    append_uleb(body, 0);
    emit_i32(body, static_cast<std::int32_t>(kCr0Slot + ho.base));
    body.push_back(kI64Load);
    body.push_back(kAlign8);
    append_uleb(body, 0);
    emit_i64(body, 16);
    body.push_back(kI64ShrU);
    body.push_back(kI32Wrap);
    emit_i32(body, 1);
    body.push_back(kI32And);
    body.push_back(kI32Or);
    emit_if_void(body);
    emit_handoff(body, ho);
    body.push_back(kBlockEnd);
}

/// Walk-level entry address (frame + index*8) left on the stack as i64.
void emit_level_addr(std::vector<std::uint8_t>& body, unsigned shift) {
    emit_local(body, kLocalGet, kTmpT);
    emit_local(body, kLocalGet, kTmpV);
    emit_i64(body, static_cast<std::int64_t>(shift));
    body.push_back(kI64ShrU);
    emit_i64(body, 0x1FF);
    body.push_back(kI64And);
    emit_i64(body, 8);
    body.push_back(kI64Mul);
    body.push_back(kI64Add);
}

/// One walk level: entry <- [kTmpT + idx(kTmpV, shift)*8]; absent or
/// out-of-RAM entries return the sentinel; kTmpT becomes the next frame.
void emit_walk_level(std::vector<std::uint8_t>& body, unsigned shift,
                     std::uint32_t ram_limit, const Handoff& ho) {
    emit_level_addr(body, shift);
    emit_local(body, kLocalSet, kTmpE);
    emit_local(body, kLocalGet, kTmpE);
    emit_i64(body, ram_limit);
    body.push_back(kI64LtU);
    body.push_back(kI32Eqz);
    emit_if_void(body);
    emit_handoff(body, ho);
    body.push_back(kBlockEnd);
    emit_local(body, kLocalGet, kTmpE);
    body.push_back(kI32Wrap);
    body.push_back(kI64Load);
    body.push_back(kAlign8);
    append_uleb(body, 0);
    emit_local(body, kLocalSet, kTmpE);
    emit_local(body, kLocalGet, kTmpE);
    emit_i64(body, 1);
    body.push_back(kI64And);
    body.push_back(kI64Eqz);
    emit_if_void(body);
    emit_handoff(body, ho);
    body.push_back(kBlockEnd);
    // next table frame
    emit_local(body, kLocalGet, kTmpE);
    emit_i64(body, static_cast<std::int64_t>(0x000FFFFFFFFFF000ULL));
    body.push_back(kI64And);
    emit_local(body, kLocalSet, kTmpT);
}

/// paddr = (entry & @p addr_mask) + (kTmpV & @p off_mask) into kTmpPd.
void emit_paddr(std::vector<std::uint8_t>& body, std::uint64_t addr_mask,
                std::uint64_t off_mask) {
    emit_local(body, kLocalGet, kTmpE);
    emit_i64(body, static_cast<std::int64_t>(addr_mask));
    body.push_back(kI64And);
    emit_local(body, kLocalGet, kTmpV);
    emit_i64(body, static_cast<std::int64_t>(off_mask));
    body.push_back(kI64And);
    body.push_back(kI64Add);
    emit_local(body, kLocalSet, kTmpPd);
}

/// Large-page check of the entry in kTmpE: emits cond (PS == 0) + `if`; the
/// 4K continuation goes into the then-branch, the large-page paddr into else.
void emit_large_page(std::vector<std::uint8_t>& body) {
    emit_local(body, kLocalGet, kTmpE);
    emit_i64(body, 0x80);
    body.push_back(kI64And);
    body.push_back(kI64Eqz); // PS == 0: continue walking
    emit_if_void(body);
}

/// Bounds-check kTmpPd for an access of @p size bytes, then leave the
/// wrapped i32 physical address on the stack.
void emit_paddr_tail(std::vector<std::uint8_t>& body, unsigned size,
                     std::uint32_t ram_limit, const Handoff& ho) {
    emit_local(body, kLocalGet, kTmpPd);
    emit_i64(body, static_cast<std::int64_t>(ram_limit) - static_cast<std::int64_t>(size));
    body.push_back(kI64LtU);
    body.push_back(kI32Eqz);
    emit_if_void(body);
    emit_handoff(body, ho);
    body.push_back(kBlockEnd);
    emit_local(body, kLocalGet, kTmpPd);
    body.push_back(kI32Wrap);
}

/// Full walk from kTmpV to a physical address (i32) on the stack. Large
/// pages nest: each PS hit resolves into kTmpPd in its else-branch and the
/// remaining 4K levels only emit inside the then-branch, so kTmpE of the
/// matching level survives to emit_paddr().
void emit_walk(std::vector<std::uint8_t>& body, unsigned size,
               std::uint32_t ram_limit, const Handoff& ho) {
    emit_i32(body, static_cast<std::int32_t>(kCr3Slot + ho.base));
    body.push_back(kI64Load);
    body.push_back(kAlign8);
    append_uleb(body, 0);
    emit_i64(body, static_cast<std::int64_t>(0x000FFFFFFFFFF000ULL));
    body.push_back(kI64And);
    emit_local(body, kLocalSet, kTmpT);
    emit_walk_level(body, 39, ram_limit, ho); // PML4E
    emit_walk_level(body, 30, ram_limit, ho); // PDPTE (+1GB large pages)
    emit_large_page(body);
    emit_walk_level(body, 21, ram_limit, ho); // PDE (+2MB large pages)
    emit_large_page(body);
    emit_walk_level(body, 12, ram_limit, ho); // PTE
    emit_paddr(body, 0x000FFFFFFFFFF000ULL, 0xFFF);
    body.push_back(kElse); // 2MB if: else-branch
    emit_paddr(body, 0xFFFFFFFE00000ULL, 0x1FFFFFULL);
    body.push_back(kBlockEnd); // close the 2MB if
    body.push_back(kElse); // 1GB if: else-branch
    emit_paddr(body, 0xFFFFC0000000ULL, 0x3FFFFFFFULL);
    body.push_back(kBlockEnd); // close the 1GB if
    emit_paddr_tail(body, size, ram_limit, ho);
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

} // namespace

void emit_handoff(std::vector<std::uint8_t>& body, std::uint32_t base, unsigned done,
                  std::uint64_t rip, std::uint32_t dirty_bits) {
    emit_handoff(body, Handoff{base, done, rip, dirty_bits});
}

void emit_flags(std::vector<std::uint8_t>& body, std::uint32_t dst, unsigned kind, bool qword,
                std::uint32_t base) {
    const std::int32_t top = qword ? 63 : 31;
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

bool lower_mem_insn(const DecodedInsn& insn, std::uint64_t rip,
                    std::vector<std::uint8_t>& body, std::uint32_t base,
                    std::uint32_t ram_limit, unsigned done, std::uint32_t dirty_bits) {
    if (insn.seg != Seg::kNone) {
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
        if (!emit_vaddr(mem, body)) {
            return false;
        }
        const Handoff ho{base, done, rip, dirty_bits};
        emit_mode_guard(body, ho);
        emit_walk(body, mem.size, ram_limit, ho);
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

    // Non-mov memory-destination forms (xchg [m],r / adc [m],r / shl [m],i
    // / neg [m] ...) must not fall through into the load path below: only
    // mov stores and reg-destination loads/ops are lowered.
    if (mem_idx == 0) {
        return false;
    }
    // Everything else needs a 32/64-bit GPR counterpart.
    if (other.kind != Operand::Kind::kReg || (other.size != 4 && other.size != 8)) {
        return false;
    }
    const auto dst = local_of(other.reg);
    if (!dst.has_value()) {
        return false;
    }
    if (!emit_vaddr(mem, body)) {
        return false;
    }

    const Handoff ho{base, done, rip, dirty_bits};
    emit_mode_guard(body, ho);
    emit_walk(body, mem.size, ram_limit, ho);
    emit_guest_load(body, mem.size);
    emit_local(body, kLocalSet, kTmpB); // t_b = loaded source (zero-extended)
    emit_local(body, kLocalGet, *dst);
    if (!qword) {
        emit_i64(body, width_mask);
        body.push_back(kI64And);
    }
    emit_local(body, kLocalSet, kTmpA);
    if (insn.mnemonic == Mnemonic::kMov) { // mov reg, [m]
        emit_local(body, kLocalGet, kTmpB);
        emit_local(body, kLocalSet, *dst);
        return true;
    }
    std::uint8_t opcode = 0;
    unsigned kind = 2;
    switch (insn.mnemonic) {
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
    emit_local(body, kLocalGet, kTmpA);
    emit_local(body, kLocalGet, kTmpB);
    body.push_back(opcode);
    const bool cmp_test =
        insn.mnemonic == Mnemonic::kCmp || insn.mnemonic == Mnemonic::kTest;
    const std::uint32_t result = cmp_test ? kTmpC : *dst;
    emit_local(body, kLocalSet, result);
    if (!qword) {
        emit_local(body, kLocalGet, result);
        emit_i64(body, width_mask);
        body.push_back(kI64And);
        emit_local(body, kLocalSet, result);
    }
    emit_flags(body, result, kind, qword, base);
    return true;
}

} // namespace micro_forge::base::detail
