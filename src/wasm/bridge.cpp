//! \file bridge.cpp
//! Combined block-bridge exports: one C crossing each for the pre-run
//! mirror flush and the post-run commit/drain/retire epilogue.

#include <cstdint>
#include <cstring>
#include <memory>

// Mirror-cell layout shared with exports.cpp (wasm_block_detail mirror).
#include "micro_forge/base/wasm_block_detail.hpp"

extern "C" {
uint32_t mf_gpr_bridge_base(void);
uint32_t mf_tlb_epoch(void);
uint32_t mf_tlb_invalidations(void);
void mf_gprs_flush(uint32_t mask);
void mf_gprs_commit(uint32_t mask);
void mf_note_store(uint64_t paddr);
void mf_set_rip(uint64_t rip);
void mf_retire(uint32_t count);
uint64_t mf_event_budget(void);
}

namespace {

/// Reads the guest-store log count cell of the bridge mirror.
std::uint32_t store_log_count(const std::uint8_t* mirror) {
    const auto* const cell = reinterpret_cast<const std::uint32_t*>( // NOLINT(*-no-int-to-ptr)
        reinterpret_cast<std::uintptr_t>(mirror) + micro_forge::base::detail::kStoreCountSlot);
    return *cell;
}

} // namespace

extern "C" {

/// @brief Flushes the mirror for a block run and reports the event budget.
/// @param[in] used_mask Bit i set mirrors GPR i; bit 31 mirrors the flags.
/// @return Guest instructions left before the next device event must
///         surface; the wasm loop must stay on the interpreter once a
///         block would outlive it.
uint64_t mf_bridge_prepare(uint32_t used_mask) {
    mf_gprs_flush(used_mask);
    // Publish the translation generation for the block-side TLB: a block
    // compares entry generations against this cell and misses on mismatch.
    const auto base = static_cast<std::uintptr_t>(mf_gpr_bridge_base());
    auto* const gen_cell = reinterpret_cast<std::uint32_t*>( // NOLINT(performance-no-int-to-ptr)
        base + micro_forge::base::detail::kTlbEpochSlot);
    const auto gen = mf_tlb_epoch();
    if (*gen_cell != gen) {
        // CR3 replaced every mapping or the guest dropped one page's
        // translation; either way cached block entries are untrustworthy.
        auto* const tlb = reinterpret_cast<void*>( // NOLINT(performance-no-int-to-ptr)
            base + micro_forge::base::detail::kTlbSlot);
        std::memset(tlb, 0,
                    static_cast<std::size_t>(micro_forge::base::detail::kTlbEntries) *
                        micro_forge::base::detail::kTlbEntryBytes);
        *gen_cell = gen;
    }
    return mf_event_budget();
}

/// @brief Post-run epilogue: commit, store-log drain, rip and retirement.
/// @param[in] next_rip Guest rip the block exited to.
/// @param[in] retire Instructions the block executed (wasm form).
/// @param[in] dirty_mask Bit i set commits GPR i; bit 31 commits flags.
void mf_bridge_finish(uint64_t next_rip, uint32_t retire, uint32_t dirty_mask) {
    mf_gprs_commit(dirty_mask);
    // Drain the store log so self-modifying code bumps the code epoch
    // before any cached block gets reused.
    const auto base = static_cast<std::uintptr_t>(mf_gpr_bridge_base());
    // NOLINTNEXTLINE(performance-no-int-to-ptr) -- wasm32 mirror base
    const auto* mirror = reinterpret_cast<const std::uint8_t*>(base);
    const auto count = store_log_count(mirror);
    if (count != 0U) {
        const auto* const log = reinterpret_cast<const std::uint64_t*>( // NOLINT(*-no-int-to-ptr)
            base + micro_forge::base::detail::kStoreLogSlot);
        for (std::uint32_t i = 0; i < count; ++i) {
            mf_note_store(log[i]);
        }
        auto* const cell = reinterpret_cast<std::uint32_t*>( // NOLINT(performance-no-int-to-ptr)
            base + micro_forge::base::detail::kStoreCountSlot);
        *cell = 0;
    }
    mf_set_rip(next_rip);
    mf_retire(retire);
}

} // extern "C"
