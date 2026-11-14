//! \file block_runner.cpp
//! Engine-side block scheduler: the JS dispatch loop (cache probe, epoch
//! check, bridge prepare/run/finish, interpreter fallback) moved into the
//! engine module so a block run costs zero C crossings for scheduling.
//!
//! JavaScript keeps exactly two jobs: compiling missing blocks (miss is
//! returned to the caller) and filling the exported indirect-function
//! table with the compiled block entry points. The table itself belongs
//! to the engine module (`__indirect_function_table`, exported), so a
//! plain C function-pointer call on a table index compiles to one
//! call_indirect — no second module, no imports.

#include <algorithm>
#include <array>
#include <cstdint>

#include "micro_forge/base/wasm_block_detail.hpp"

// Engine C ABI used directly (same module: plain calls, no crossings).
extern "C" {
uint32_t mf_rip_low(void);
uint32_t mf_rip_high(void);
uint64_t mf_code_epoch(void);
uint64_t mf_bridge_prepare(uint32_t used_mask);
void mf_bridge_finish(uint64_t next_rip, uint32_t retire, uint32_t dirty_mask);
uint64_t mf_icount(void);
uint64_t mf_run(uint64_t max_instructions);
uint32_t mf_gpr_bridge_base(void);
}

namespace {

/// Registry slots: power of two, linear probing.
constexpr std::uint32_t kSlots = 65536;
/// Consecutive skips before re-probing a zero-progress block.
constexpr std::uint32_t kSkipProbe = 64;
/// Interpreter batch on the no-block / suppressed / budget-short path.
/// 实测 40M（固定核交替）：32 档 15.2 Mips，512 档 25.9，1024 档 27.5，
/// 2048 档 28.0，4096 档反而掉（块机会被延迟太多）。批次本身不改语义，
/// mf_run 逐条处理事件且自带预算；只是隔更多条才回查块机会。
constexpr std::uint64_t kFallbackBatch = 1024;

/// Entry lifecycle states.
enum class State : std::uint8_t { kEmpty = 0, kActive = 1, kRefused = 2, kTomb = 3 };

/// One cache entry: guest rip keyed, epoch-fenced block metadata.
struct Entry {
    std::uint64_t rip;   ///< guest rip this block was compiled at.
    std::uint64_t epoch; ///< code epoch at compile time.
    std::uint32_t fn;    ///< indirect-function-table slot of the block.
    std::uint32_t count; ///< instructions the block retires.
    std::uint32_t used;  ///< mirror flush mask (bit 31 = flags).
    std::uint32_t dirty; ///< mirror commit mask (bit 31 = flags).
    std::uint32_t skip;  ///< remaining suppression rounds (done=0 blocks).
    std::uint32_t execs; ///< times the entry was executed (diagnostics).
    State state;         ///< lifecycle state.
};

std::array<Entry, kSlots> g_entries{};
std::uint32_t g_next_fn = 0;            ///< next free table slot (assigned upward).
std::uint64_t g_retired = 0;            ///< instructions retired by the last run.
std::uint64_t g_exit = 0;               ///< packed terminal exit, if any.
std::uint32_t g_execs = 0;              ///< block runs started by the last run.
std::uint64_t g_miss = 0;               ///< rip the caller must compile.
std::uint64_t g_batch = kFallbackBatch; ///< interpreter batch per fallback.

/// Hash of a guest rip into the registry (fibonacci-ish, low bits).
std::uint32_t hash_of(std::uint64_t rip) {
    std::uint32_t n = static_cast<std::uint32_t>(rip) ^ static_cast<std::uint32_t>(rip >> 32U);
    n ^= n >> 16U;
    n *= 0x7FEB352AU;
    n ^= n >> 15U;
    return n & (kSlots - 1U);
}

/// Linear-probe lookup; empty cell stops the probe chain.
Entry* find(std::uint64_t rip) {
    std::uint32_t i = hash_of(rip);
    for (std::uint32_t n = 0; n < kSlots; ++n, i = (i + 1U) & (kSlots - 1U)) {
        Entry* p = &g_entries[i];
        if (p->state == State::kEmpty) {
            return nullptr;
        }
        if (p->state != State::kTomb && p->rip == rip) {
            return p;
        }
    }
    return nullptr;
}

/// Mirror u32 cell read at byte offset @p off from the bridge base.
std::uint32_t mirror_u32(std::uint32_t off) {
    // wasm32 mirror base
    const auto* mirror =
        reinterpret_cast<const std::uint32_t*>( // NOLINT(performance-no-int-to-ptr)
            static_cast<std::uintptr_t>(mf_gpr_bridge_base()));
    return mirror[off / sizeof(std::uint32_t)];
}

} // namespace

extern "C" {

/// @brief Clears the runner registry (epoch changes make entries stale).
/// @param[in] table_base First table slot safe for blocks (JS passes the
///        exported table's length; lower slots belong to the engine).
void mf_runner_reset(std::uint32_t table_base) {
    g_entries.fill(Entry{});
    g_next_fn = table_base;
}

/// @brief Registers a compiled block and returns its table slot.
/// @param[in] rip_lo Low 32 bits of the guest rip.
/// @param[in] rip_hi High 32 bits of the guest rip.
/// @param[in] count Instructions the block retires when it completes.
/// @param[in] used Mirror flush mask (bit 31 = flags).
/// @param[in] dirty Mirror commit mask (bit 31 = flags).
/// @return Table slot the caller must fill with the block function, or
///         -1 when the registry is full (caller evicts and retries).
std::int32_t mf_runner_put(std::uint32_t rip_lo, std::uint32_t rip_hi, std::uint32_t count,
                           std::uint32_t used, std::uint32_t dirty) {
    const std::uint64_t rip = (static_cast<std::uint64_t>(rip_hi) << 32U) | rip_lo;
    Entry* p = find(rip);
    if (p == nullptr) {
        std::uint32_t i = hash_of(rip);
        for (std::uint32_t n = 0; n < kSlots; ++n, i = (i + 1U) & (kSlots - 1U)) {
            p = &g_entries[i];
            if (p->state == State::kEmpty || p->state == State::kTomb) {
                break;
            }
        }
        if (p->state != State::kEmpty && p->state != State::kTomb) {
            return -1; // registry full: caller evicts first
        }
    }
    const std::uint32_t slot = g_next_fn++;
    *p = Entry{
        .rip = rip,
        .epoch = mf_code_epoch(),
        .fn = slot,
        .count = count,
        .used = used,
        .dirty = dirty,
        .skip = 0,
        .state = State::kActive,
    };
    return static_cast<std::int32_t>(slot);
}

/// @brief Marks a rip as compile-refused so the runner batches it out.
/// @param[in] rip_lo Low 32 bits of the guest rip.
/// @param[in] rip_hi High 32 bits of the guest rip.
void mf_runner_refuse(std::uint32_t rip_lo, std::uint32_t rip_hi) {
    const std::uint64_t rip = (static_cast<std::uint64_t>(rip_hi) << 32U) | rip_lo;
    Entry* p = find(rip);
    if (p == nullptr) {
        std::uint32_t i = hash_of(rip);
        for (std::uint32_t n = 0; n < kSlots; ++n, i = (i + 1U) & (kSlots - 1U)) {
            p = &g_entries[i];
            if (p->state == State::kEmpty || p->state == State::kTomb) {
                break;
            }
        }
    }
    if (p != nullptr) {
        *p = Entry{
            .rip = rip,
            .epoch = mf_code_epoch(),
            .fn = 0,
            .count = 0,
            .used = 0,
            .dirty = 0,
            .skip = 0,
            .state = State::kRefused,
        };
    }
}

/// @brief Re-stamps an entry's epoch after its bytes proved unchanged.
/// @param[in] rip_lo Low 32 bits of the guest rip.
/// @param[in] rip_hi High 32 bits of the guest rip.
/// @return 1 when the entry was found and stamped, 0 otherwise.
uint32_t mf_runner_reepoch(std::uint32_t rip_lo, std::uint32_t rip_hi) {
    Entry* p = find((static_cast<std::uint64_t>(rip_hi) << 32U) | rip_lo);
    if (p == nullptr || p->state != State::kActive) {
        return 0;
    }
    p->epoch = mf_code_epoch();
    return 1;
}

/// @brief Drops one rip from the registry (eviction / epoch discard).
/// @param[in] rip_lo Low 32 bits of the guest rip.
/// @param[in] rip_hi High 32 bits of the guest rip.
void mf_runner_remove(std::uint32_t rip_lo, std::uint32_t rip_hi) {
    Entry* p = find((static_cast<std::uint64_t>(rip_hi) << 32U) | rip_lo);
    if (p != nullptr) {
        p->state = State::kTomb;
    }
}

/// @brief Overrides the interpreter fallback batch size.
/// @param[in] batch Instructions per fallback run, clamped to 1..4096.
void mf_runner_set_batch(std::uint64_t batch) {
    g_batch = std::clamp<std::uint64_t>(batch, 1U, 4096U);
}

/// @brief Instructions retired by the last mf_runner_run call.
uint64_t mf_runner_retired() {
    return g_retired;
}

/// @brief Packed terminal exit of the last mf_runner_run call.
uint64_t mf_runner_exit() {
    return g_exit;
}

/// @brief Guest rip the last mf_runner_run miss wants compiled (low half).
uint32_t mf_runner_miss_lo() {
    return static_cast<std::uint32_t>(g_miss);
}

/// @brief Guest rip the last mf_runner_run miss wants compiled (high half).
uint32_t mf_runner_miss_hi() {
    return static_cast<std::uint32_t>(g_miss >> 32U);
}

/// @brief Block executions started by the last mf_runner_run call.
uint32_t mf_runner_execs() {
    return g_execs;
}

/// @brief Dumps the hottest registry entries for offline analysis.
/// @param[out] out Address of a u32 array, 4 words per entry.
/// @param[in] max Entry capacity of @p out.
/// @return Number of entries written, hottest first.
uint32_t mf_runner_dump(std::uint32_t out, std::uint32_t max) {
    auto* const words =
        reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(out)); // NOLINT
    std::uint32_t written = 0;
    for (const Entry& e : g_entries) {
        if (e.state != State::kActive || e.execs == 0U) {
            continue;
        }
        const auto quad = [&words](std::uint32_t i) {
            return words + (static_cast<std::size_t>(i) * 4U);
        };
        std::uint32_t pos = written;
        while (pos > 0U && quad(pos - 1U)[3] < e.execs) {
            quad(pos)[0] = quad(pos - 1U)[0];
            quad(pos)[1] = quad(pos - 1U)[1];
            quad(pos)[2] = quad(pos - 1U)[2];
            quad(pos)[3] = quad(pos - 1U)[3];
            --pos;
        }
        quad(pos)[0] = static_cast<std::uint32_t>(e.rip);
        quad(pos)[1] = static_cast<std::uint32_t>(e.rip >> 32U);
        quad(pos)[2] = e.count;
        quad(pos)[3] = e.execs;
        if (written < max) {
            ++written;
        }
    }
    return written;
}

/// @brief Engine-side scheduling loop over registered blocks.
/// @param[in] budget Instruction budget for this call.
/// @param[in] burst Max scheduling iterations before returning.
/// @return 0 budget reached, 1 compile miss (new rip), 2 stale-epoch miss,
///         3 terminal exit, 4 burst limit hit. Misses leave the rip in
///         mf_runner_miss_lo/hi; retired totals in mf_runner_retired.
uint32_t mf_runner_run(uint64_t budget, uint32_t burst) {
    g_retired = 0;
    g_exit = 0;
    g_execs = 0;
    for (std::uint32_t i = 0; i < burst && g_retired < budget; ++i) {
        const std::uint64_t rip = (static_cast<std::uint64_t>(mf_rip_high()) << 32U) | mf_rip_low();
        Entry* p = find(rip);
        if (p == nullptr || (p->state == State::kActive && p->epoch != mf_code_epoch())) {
            g_miss = rip;
            return p == nullptr ? 1U : 2U;
        }
        if (p->state == State::kActive) {
            if (p->skip != 0U) {
                --p->skip;
            } else if (mf_bridge_prepare(p->used | 0x80000000U) >= p->count) {
                using BlockFn = std::uint64_t (*)();
                const std::uint64_t next =
                    reinterpret_cast<BlockFn>(p->fn)(); // NOLINT(*-no-int-to-ptr)
                const bool handed = mirror_u32(micro_forge::base::detail::kHandedSlot) == 1U;
                const std::uint32_t done =
                    handed ? mirror_u32(micro_forge::base::detail::kDoneSlot) : p->count;
                const std::uint32_t hd =
                    handed ? mirror_u32(micro_forge::base::detail::kHandoffDirtySlot) : 0U;
                if (handed && done == 0U) {
                    p->skip = kSkipProbe; // zero-progress handoff: suppress
                }
                mf_bridge_finish(next, done, p->dirty | hd | 0x80000000U);
                g_retired += done;
                ++p->execs;
                ++g_execs;
                if (handed) {
                    // next rip often has a block: single interpreter step
                    const std::uint64_t before = mf_icount();
                    g_exit = mf_run(1);
                    g_retired += mf_icount() - before;
                    if ((g_exit & 0xFFU) != 0U) {
                        return 3U;
                    }
                }
                continue;
            }
        }
        // no block, suppressed, or the event budget would outlive it:
        // short interpreter batch (mf_run caps at its own event budget)
        const std::uint64_t remain = budget - g_retired;
        const std::uint64_t before = mf_icount();
        const std::uint64_t batch =
            std::clamp<std::uint64_t>(remain, 1U, g_batch); // always progress
        g_exit = mf_run(batch);
        g_retired += mf_icount() - before;
        if ((g_exit & 0xFFU) != 0U) {
            return 3U;
        }
    }
    return g_retired >= budget ? 0U : 4U;
}

} // extern "C"
