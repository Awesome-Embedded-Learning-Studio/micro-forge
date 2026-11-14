/**
 * @file wasm_block_walk.cpp
 * @brief Inline four-level page-table walk for wasm-block memory operands.
 * @version 0.1.0
 *
 * The walk reads guest page tables straight out of the engine linear
 * memory (page tables live in guest RAM, a direct window). Any absent or
 * out-of-RAM entry hands the rest of the block back to the runner, which
 * takes one interpreter step through the one true implementation.
 */

#include "micro_forge/base/wasm_block_detail.hpp"
#include "micro_forge/base/wasm_block_internal.hpp"
#include "micro_forge/base/wasm_emit.hpp"

#include <cstdint>
#include <vector>

namespace micro_forge::base::detail {

/// Kernel-mode guard: cpl != 0 or CR0.WP=1 returns the sentinel (the
/// interpreter re-checks user/write permissions we cannot mirror cheaply).
/// Marks the mirror handed flag, records done/dirty and returns @p ho.rip.
void emit_handoff(std::vector<std::uint8_t>& body, const Handoff& ho, unsigned reason) {
    // Always rewrite the tag: a stale nonzero value would mislabel the
    // next handoff that comes from a different refusal site.
    emit_i32(body, static_cast<std::int32_t>(kHandoffReasonSlot + ho.base));
    emit_i32(body, static_cast<std::int32_t>(reason));
    body.push_back(kI32Store);
    body.push_back(kAlign4);
    append_uleb(body, 0);
    // the early return skips the exit bridge: push everything the block
    // has already written back to the mirror before handing control over
    for (std::uint32_t local = 0; local < kNumGprs; ++local) {
        if (((ho.dirty_bits >> local) & 1U) != 0U) {
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

/// Early block exit with an instruction count (taken side of a jcc).
void emit_branch_exit(std::vector<std::uint8_t>& body, const Handoff& ho) {
    for (std::uint32_t local = 0; local < kNumGprs; ++local) {
        if (((ho.dirty_bits >> local) & 1U) != 0U) {
            emit_i32(body, static_cast<std::int32_t>(local * 8U));
            emit_local(body, kLocalGet, local);
            emit_store(body, ho.base);
        }
    }
    emit_i32(body, static_cast<std::int32_t>(kExitDoneSlot + ho.base));
    emit_i32(body, static_cast<std::int32_t>(ho.done));
    body.push_back(kI32Store);
    body.push_back(kAlign4);
    append_uleb(body, 0);
    // Only the part of the slice that ran may reach the CPU.
    emit_i32(body, static_cast<std::int32_t>(kHandoffDirtySlot + ho.base));
    emit_i32(body, static_cast<std::int32_t>(ho.dirty_bits));
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

namespace {
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
void emit_walk_level(std::vector<std::uint8_t>& body, unsigned shift, std::uint32_t ram_limit,
                     const Handoff& ho) {
    emit_level_addr(body, shift);
    emit_local(body, kLocalSet, kTmpE);
    emit_local(body, kLocalGet, kTmpE);
    emit_i64(body, ram_limit);
    body.push_back(kI64LtU);
    body.push_back(kI32Eqz);
    emit_if_void(body);
    emit_handoff(body, ho, 1);
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
    emit_handoff(body, ho, 2);
    body.push_back(kBlockEnd);
    // next table frame
    emit_local(body, kLocalGet, kTmpE);
    emit_i64(body, static_cast<std::int64_t>(0x000FFFFFFFFFF000ULL));
    body.push_back(kI64And);
    emit_local(body, kLocalSet, kTmpT);
}

/// paddr = (entry & @p addr_mask) + (kTmpV & @p off_mask) into kTmpPd.
void emit_paddr(std::vector<std::uint8_t>& body, std::uint64_t addr_mask, std::uint64_t off_mask) {
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
void emit_paddr_tail(std::vector<std::uint8_t>& body, unsigned size, std::uint32_t ram_limit,
                     const Handoff& ho) {
    emit_local(body, kLocalGet, kTmpPd);
    emit_i64(body, static_cast<std::int64_t>(ram_limit) - static_cast<std::int64_t>(size));
    body.push_back(kI64LtU);
    body.push_back(kI32Eqz);
    emit_if_void(body);
    emit_handoff(body, ho, 3);
    body.push_back(kBlockEnd);
    emit_local(body, kLocalGet, kTmpPd);
    body.push_back(kI32Wrap);
}

/// Full walk from kTmpV to a physical address (i32) on the stack. Large
/// pages nest: each PS hit resolves into kTmpPd in its else-branch and the
/// remaining 4K levels only emit inside the then-branch, so kTmpE of the
/// matching level survives to emit_paddr().
/// Entry address of the block-TLB slot for kTmpV into kTmpE (i64 offset).
// NOLINTNEXTLINE(misc-use-internal-linkage,misc-const-correctness)
void emit_tlb_addr(std::vector<std::uint8_t>& body, std::uint32_t base) {
    emit_local(body, kLocalGet, kTmpV);
    emit_i64(body, 12U);
    body.push_back(kI64ShrU);
    emit_i64(body, static_cast<std::int64_t>(kTlbEntries - 1U));
    body.push_back(kI64And);
    emit_i64(body, static_cast<std::int64_t>(kTlbEntryBytes));
    body.push_back(kI64Mul);
    emit_i64(body, static_cast<std::int64_t>(kTlbSlot) + base);
    body.push_back(kI64Add);
    emit_local(body, kLocalSet, kTmpE);
}

/// Probes the block TLB; leaves the hit condition on the stack and the
/// entry address in kTmpE.
void emit_tlb_probe(std::vector<std::uint8_t>& body, std::uint32_t base, bool write) {
    emit_tlb_addr(body, base);
    // Entries are only valid for the current mapping generation: prepare
    // clears the table whenever CR3 or INVLPG changes it, so the probe only
    // has to match the page. (The generation cell is still published for
    // that clear decision, just not compared per access.)
    // vaddr-page mismatch -> miss
    emit_local(body, kLocalGet, kTmpE);
    body.push_back(kI32Wrap);
    body.push_back(kI64Load);
    body.push_back(kAlign8);
    append_uleb(body, 0);
    emit_local(body, kLocalGet, kTmpV);
    emit_i64(body, -4096);
    body.push_back(kI64And);
    body.push_back(kI64Eq);
    body.push_back(kI32Eqz); // miss on page mismatch
    // user-mode permission: cpl != 0 requires the entry's U/S bit, otherwise
    // the access must fall through to the walk (which re-checks and hands
    // the faulting instruction to the interpreter).
    emit_local(body, kLocalGet, kPermCpl);
    body.push_back(kI32Eqz); // cpl == 0 -> allowed
    emit_local(body, kLocalGet, kTmpE);
    body.push_back(kI32Wrap);
    body.push_back(kI32Load);
    body.push_back(kAlign4);
    append_uleb(body, 16);
    emit_i32(body, 1);
    body.push_back(kI32And); // entry U/S
    body.push_back(kI32Or);  // permission ok
    body.push_back(kI32Eqz);
    body.push_back(kI32Or); // miss
    if (write) {
        // CR0.WP=1 turns a kernel write to a read-only page into #PF; the
        // block only checks the page's RW bit and hands the fault back.
        emit_local(body, kLocalGet, kPermWp);
        emit_local(body, kLocalGet, kTmpE);
        body.push_back(kI32Wrap);
        body.push_back(kI32Load);
        body.push_back(kAlign4);
        append_uleb(body, 16);
        emit_i32(body, 2);
        body.push_back(kI32And); // entry RW
        body.push_back(kI32Eqz); // read-only page
        body.push_back(kI32And);
        body.push_back(kI32Or); // miss
    }
    body.push_back(kI32Eqz); // hit condition
}

/// Fills the TLB entry at kTmpE with the translation of kTmpV to kTmpPd.
// NOLINTNEXTLINE(misc-use-internal-linkage,misc-const-correctness)
void emit_tlb_fill(std::vector<std::uint8_t>& body) {
    emit_local(body, kLocalGet, kTmpE);
    body.push_back(kI32Wrap);
    emit_local(body, kLocalGet, kTmpV);
    emit_i64(body, -4096);
    body.push_back(kI64And);
    body.push_back(kI64Store);
    body.push_back(kAlign8);
    append_uleb(body, 0);
    emit_local(body, kLocalGet, kTmpE);
    body.push_back(kI32Wrap);
    emit_local(body, kLocalGet, kTmpPd);
    body.push_back(kI32Wrap);
    emit_i64(body, -4096);
    body.push_back(kI32Wrap);
    body.push_back(kI32And);
    body.push_back(kI32Store);
    body.push_back(kAlign4);
    append_uleb(body, 8);
    emit_local(body, kLocalGet, kTmpE);
    body.push_back(kI32Wrap);
    emit_i32(body, 0); // generation slot kept for the entry layout
    body.push_back(kI32Store);
    body.push_back(kAlign4);
    append_uleb(body, 12);
    // permissions: U/S and R/W bits parked in kTmpCi by the walk tail
    emit_local(body, kLocalGet, kTmpE);
    body.push_back(kI32Wrap);
    emit_local(body, kLocalGet, kTmpCi);
    body.push_back(kI32Wrap);
    body.push_back(kI32Store);
    body.push_back(kAlign4);
    append_uleb(body, 16);
}

} // namespace

// Called from wasm_block_mem.cpp through wasm_block_internal.hpp.
// NOLINTNEXTLINE(misc-use-internal-linkage,misc-const-correctness)
void emit_walk(std::vector<std::uint8_t>& body, unsigned size, std::uint32_t ram_limit,
               const Handoff& ho, bool write) {
    emit_tlb_probe(body, ho.base, write);
    body.push_back(kIfVoid);
    body.push_back(kI32ResultType);
    // hit: paddr = cached page | page offset
    emit_local(body, kLocalGet, kTmpE);
    body.push_back(kI32Wrap);
    body.push_back(kI32Load);
    body.push_back(kAlign4);
    append_uleb(body, 8);
    emit_local(body, kLocalGet, kTmpV);
    body.push_back(kI32Wrap);
    emit_i32(body, 4095);
    body.push_back(kI32And);
    body.push_back(kI32Or);
    body.push_back(kElse);
    // miss: four-level walk, then cache the translation
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
    body.push_back(kElse);     // 1GB if: else-branch
    emit_paddr(body, 0xFFFFC0000000ULL, 0x3FFFFFFFULL);
    body.push_back(kBlockEnd); // close the 1GB if
    // user-mode access to a supervisor page: hand the instruction to the
    // interpreter so the real #PF (CR2, error code, kernel handler) fires.
    emit_local(body, kLocalGet, kPermCpl);
    emit_local(body, kLocalGet, kTmpE);
    emit_i64(body, 2);
    body.push_back(kI64ShrU);
    emit_i64(body, 1);
    body.push_back(kI64And);
    body.push_back(kI32Wrap);
    body.push_back(kI32Eqz); // U/S == 0
    body.push_back(kI32And);
    emit_if_void(body);
    emit_handoff(body, ho, 4);
    body.push_back(kBlockEnd);
    if (write) {
        // CR0.WP=1 write to a read-only page: hand the instruction back so
        // the interpreter raises the real #PF.
        emit_local(body, kLocalGet, kPermWp);
        emit_local(body, kLocalGet, kTmpE);
        emit_i64(body, 1);
        body.push_back(kI64ShrU);
        emit_i64(body, 1);
        body.push_back(kI64And);
        body.push_back(kI32Wrap);
        body.push_back(kI32Eqz);
        body.push_back(kI32And);
        emit_if_void(body);
        emit_handoff(body, ho, 5);
        body.push_back(kBlockEnd);
    }
    // Park the entry's U/S bit now: kTmpE is about to be reused as the TLB
    // entry address by emit_tlb_addr, and emit_tlb_fill needs the bit.
    emit_local(body, kLocalGet, kTmpE);
    emit_i64(body, 2);
    body.push_back(kI64ShrU);
    emit_i64(body, 3);
    body.push_back(kI64And);
    emit_local(body, kLocalSet, kTmpCi); // bit0 = U/S, bit1 = R/W
    emit_paddr_tail(body, size, ram_limit, ho);
    // emit_paddr_tail left the i32 paddr on the stack: park it, fill the
    // entry, then hand the value back as the if result.
    emit_local(body, kLocalSet, kTmpP);
    emit_tlb_addr(body, ho.base);
    emit_tlb_fill(body);
    emit_local(body, kLocalGet, kTmpP);
    body.push_back(kBlockEnd); // close the TLB if
}

} // namespace micro_forge::base::detail
