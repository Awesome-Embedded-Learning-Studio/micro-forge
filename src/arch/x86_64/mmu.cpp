/**
 * @file mmu.cpp
 * @brief Software MMU walk and TLB maintenance (batch 6a).
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/mmu.hpp"

namespace micro_forge::arch::x86_64 {

namespace {
constexpr uint64_t kPageShift = 12;
// Physical address bits only (12..51). A full ~0xFFF mask would
// carry the NX bit (63) into frame arithmetic -- the batch-6
// review's blocker: PTE 0x80000000192005 became paddr 0x80000000192000.
constexpr uint64_t kFrameMask = 0x000FFFFFFFFFF000ULL;
constexpr uint64_t kPresentBit = 1ULL;
constexpr unsigned kPml4Shift = 39;
constexpr unsigned kPdptShift = 30;
constexpr unsigned kPdShift = 21;

/// Index into one 512-entry table level (shift is the page-table level
/// shift applied to the virtual ADDRESS, not the page number).
[[nodiscard]] uint64_t level_index(uint64_t vaddr, unsigned shift) {
    return (vaddr >> shift) & 0x1FFULL;
}
} // namespace

bool Mmu::read_pte(uint64_t paddr, uint64_t& value) {
    const auto read = bus_.read_u64(paddr);
    if (!read.has_value()) {
        return false;
    }
    value = *read;
    return true;
}

void Mmu::write_pte(uint64_t paddr, uint64_t value) {
    const auto written = bus_.write_u64(paddr, value);
    (void)written; // page-table writes cannot fail on a RAM-backed bus
}

namespace {

/// Applies the permission rules and returns the #PF error code (0 when
/// the access may proceed). Shared by the fast TLB path and the walk.
uint64_t check_permissions(uint64_t flags, bool write, bool instruction, bool user, bool wp,
                           bool nxe) {
    uint64_t error = 0;
    if ((flags & Mmu::kPresent) == 0) {
        error |= Mmu::kErrPresent;
    }
    if (write) {
        error |= Mmu::kErrWrite;
    }
    if (user) {
        error |= Mmu::kErrUser;
    }
    if (write && (flags & Mmu::kWrite) == 0 && (user || wp)) {
        return error; // read-only page; supervisor writes pass only with WP=0
    }
    if (user && (flags & Mmu::kUser) == 0) {
        return error | Mmu::kErrUser;
    }
    if (instruction && nxe && (flags & Mmu::kNx) != 0) {
        return error | Mmu::kErrInstruction;
    }
    return 0;
}

} // namespace

Mmu::Walk Mmu::walk(uint64_t vaddr, bool write, bool instruction, bool user, uint64_t cr3,
                    uint64_t cr0, uint64_t efer) {
    Walk w;
    const bool nxe = (efer & (1ULL << 11U)) != 0; // EFER.NXE
    const bool wp = (cr0 & (1ULL << 16U)) != 0;   // CR0.WP
    const auto fail = [&](uint64_t error) {
        w.ok = false;
        w.error = error;
        return w;
    };

    // PML4 -> PDPT -> PD -> PT, one physical 8-byte read per level.
    uint64_t e0 = 0;
    if (!read_pte((cr3 & kFrameMask) + (level_index(vaddr, kPml4Shift) * 8U), e0) ||
        (e0 & kPresentBit) == 0) {
        return fail(0);
    }
    uint64_t e1 = 0;
    if (!read_pte((e0 & kFrameMask) + (level_index(vaddr, kPdptShift) * 8U), e1) ||
        (e1 & kPresentBit) == 0) {
        return fail(0);
    }
    if ((e1 & kPageSize) != 0) { // 1 GiB large page
        if ((e1 & 0x3FF000ULL) != 0) {
            return fail(Mmu::kErrReserved);
        }
        uint64_t updated = e1 | Mmu::kAccessed;
        if (write) {
            updated |= Mmu::kDirty;
        }
        if (updated != e1) {
            write_pte((e0 & kFrameMask) + (level_index(vaddr, kPdptShift) * 8U), updated);
        }
        w.paddr = (e1 & ~0x3FFFFFFFULL) + (vaddr & 0x3FFFFFFFULL);
        w.flags = e1;
        w.ok = true;
    } else {
        uint64_t e2 = 0;
        if (!read_pte((e1 & kFrameMask) + (level_index(vaddr, kPdShift) * 8U), e2) ||
            (e2 & kPresentBit) == 0) {
            return fail(0);
        }
        if ((e2 & kPageSize) != 0) { // 2 MiB large page
            uint64_t updated = e2 | Mmu::kAccessed;
            if (write) {
                updated |= Mmu::kDirty;
            }
            if (updated != e2) {
                write_pte((e1 & kFrameMask) + (level_index(vaddr, kPdShift) * 8U), updated);
            }
            w.paddr = (e2 & ~0x1FFFFFULL) + (vaddr & 0x1FFFFFULL);
            w.flags = e2;
            w.ok = true;
        } else {
            uint64_t e3 = 0;
            if (!read_pte((e2 & kFrameMask) + (level_index(vaddr, kPageShift) * 8U), e3) ||
                (e3 & kPresentBit) == 0) {
                return fail(0);
            }
            uint64_t updated = e3 | Mmu::kAccessed;
            if (write) {
                updated |= Mmu::kDirty;
            }
            if (updated != e3) {
                write_pte((e2 & kFrameMask) + (level_index(vaddr, kPageShift) * 8U), updated);
            }
            w.paddr = (e3 & kFrameMask) + (vaddr & 0xFFFULL);
            w.flags = e3;
            w.ok = true;
        }
    }
    if (!w.ok) {
        return w;
    }
    constexpr uint64_t reserved_mask = 0x7FFULL << 52U; // bits 52..62
    if ((w.flags & reserved_mask) != 0 || (!nxe && (w.flags & kNx) != 0)) {
        return fail(w.error | Mmu::kErrReserved);
    }
    const uint64_t perm_error = check_permissions(w.flags, write, instruction, user, wp, nxe);
    if (perm_error != 0) {
        return fail(perm_error);
    }
    return w;
}

TranslateResult Mmu::translate(uint64_t vaddr, bool write, bool instruction, bool user,
                               uint64_t cr3, uint64_t cr0, uint64_t efer) {
    if (!paging_) {
        return TranslateResult{.ok = true, .paddr = vaddr, .error = 0};
    }
    const bool nxe = (efer & (1ULL << 11U)) != 0;
    const bool wp = (cr0 & (1ULL << 16U)) != 0;
    const uint64_t vpn = vaddr >> kPageShift;
    TlbEntry& entry = table_[vpn & (kEntries - 1U)];
    if (entry.flags != 0 && entry.tag == vpn) {
        // Permissions are re-checked on every access: the TLB caches the
        // mapping, never the decision.
        const uint64_t error = check_permissions(entry.flags, write, instruction, user, wp, nxe);
        if (error == 0) {
            return TranslateResult{
                .ok = true,
                .paddr = entry.frame + (vaddr & 0xFFFULL),
                .error = 0,
            };
        }
        return TranslateResult{
            .ok = false,
            .paddr = 0,
            .error = error,
        };
    }

    const Walk w = walk(vaddr, write, instruction, user, cr3, cr0, efer);
    if (!w.ok) {
        return TranslateResult{.ok = false, .paddr = 0, .error = w.error};
    }
    entry.tag = vpn;
    entry.frame = w.paddr & kFrameMask;
    entry.flags = w.flags == 0 ? kPresent : w.flags;
    return TranslateResult{.ok = true, .paddr = w.paddr, .error = 0};
}

} // namespace micro_forge::arch::x86_64
