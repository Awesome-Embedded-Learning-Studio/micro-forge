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

#include <cstdint>
#include <vector>

namespace micro_forge::base::detail {

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

/// Marks the mirror handed flag, records done/dirty and returns @p ho.rip.
void emit_handoff(std::vector<std::uint8_t>& body, const Handoff& ho) {
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
    emit_handoff(body, ho);
    body.push_back(kBlockEnd);
    emit_local(body, kLocalGet, kTmpPd);
    body.push_back(kI32Wrap);
}
} // namespace

/// Full walk from kTmpV to a physical address (i32) on the stack. Large
/// pages nest: each PS hit resolves into kTmpPd in its else-branch and the
/// remaining 4K levels only emit inside the then-branch, so kTmpE of the
/// matching level survives to emit_paddr().
void emit_walk(std::vector<std::uint8_t>& body, unsigned size, std::uint32_t ram_limit,
               const Handoff& ho) {
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
    emit_paddr_tail(body, size, ram_limit, ho);
}

} // namespace micro_forge::base::detail
