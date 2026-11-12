/**
 * @file mmu.hpp
 * @brief Software MMU: 4096-entry direct-mapped TLB over 4-level walk.
 * @version 0.1.0
 *
 * Shape follows the spec 5.3 decision (QEMU's single table + addend,
 * not v86's full-capacity TLB): one 4096-entry direct-mapped table per
 * privilege level, index = (vpn & 4095), tag = the virtual page number,
 * entry = page-frame base + permission bits. The fast path is tag
 * compare plus an add; a miss walks the four levels (four guest-memory
 * reads) and writes A/D bits back.
 *
 * Invalidation: CR3 writes flush the whole table, INVLPG drops one
 * entry. PCID is not implemented (spec: deferred).
 *
 * Until a boot shim installs a page table, the MMU runs in pass-through
 * mode (virtual == physical), so the batch-3/5 tests keep their flat
 * view. `set_paging(true)` is the boot shim's switch.
 */

#pragma once

#include <array>
#include <cstdint>

#include "micro_forge/kernel/bus/bus.hpp"

namespace micro_forge::arch::x86_64 {

/// One direct-mapped TLB entry.
struct TlbEntry {
    /// Virtual page number this entry caches (valid when flags has kValid).
    uint64_t tag = 0;
    /// Guest-physical base of the page frame.
    uint64_t frame = 0;
    /// Permission bits copied from the walked entry.
    uint64_t flags = 0;
};

/**
 * @brief Translation result.
 */
struct TranslateResult {
    /// Whether the access translated.
    bool ok = false;
    /// Guest-physical address on success.
    uint64_t paddr = 0;
    /// #PF error code on failure (P|W|U|RSVD|I/D bits).
    uint64_t error = 0;
};

/**
 * @brief Software MMU over the guest bus.
 */
class Mmu {
public:
    static constexpr unsigned kEntries = 4096;
    /// Page-table entry permission bits (Intel layout).
    static constexpr uint64_t kPresent = uint64_t{1} << 0U;
    static constexpr uint64_t kWrite = uint64_t{1} << 1U;
    static constexpr uint64_t kUser = uint64_t{1} << 2U;
    static constexpr uint64_t kWriteThrough = uint64_t{1} << 3U;
    static constexpr uint64_t kCacheDisable = uint64_t{1} << 4U;
    static constexpr uint64_t kAccessed = uint64_t{1} << 5U;
    static constexpr uint64_t kDirty = uint64_t{1} << 6U;
    static constexpr uint64_t kPageSize = uint64_t{1} << 7U;
    static constexpr uint64_t kNx = uint64_t{1} << 63U;
    /// #PF error-code bits.
    static constexpr uint64_t kErrPresent = uint64_t{1} << 0U;
    static constexpr uint64_t kErrWrite = uint64_t{1} << 1U;
    static constexpr uint64_t kErrUser = uint64_t{1} << 2U;
    static constexpr uint64_t kErrReserved = uint64_t{1} << 3U;
    static constexpr uint64_t kErrInstruction = uint64_t{1} << 4U;

    /**
     * @brief Binds the MMU to the guest bus (page tables live in guest
     *        memory, read with physical addresses only).
     *
     * @param[in] bus Guest memory bus.
     */
    explicit Mmu(kernel::Bus& bus) : bus_(bus) {}

    /// @brief Enables or disables paging (pass-through when off).
    void set_paging(bool enabled) { paging_ = enabled; }
    /// @brief Whether paging is enabled.
    [[nodiscard]] bool paging() const { return paging_; }
    /// @brief CR3 write: flush everything.
    void flush() { table_.fill(TlbEntry{}); }
    /// @brief INVLPG: drop the entry for this virtual address.
    void invalidate(uint64_t vaddr) { table_[(vaddr >> 12U) & (kEntries - 1U)] = TlbEntry{}; }

    /**
     * @brief Translates one access.
     *
     * @param[in] vaddr Virtual address.
     * @param[in] write True for a store (W bit and CR0.WP semantics).
     * @param[in] instruction True for an instruction fetch (NX check).
     * @param[in] user True for CPL=3 accesses.
     * @param[in] cr3 Page-table root (physical).
     * @param[in] cr0 CR0 (WP bit chooses the read-only-write rule).
     * @param[in] efer EFER (NXE bit gates the NX check).
     * @return Translation outcome plus the fault code when refused.
     */
    [[nodiscard]] TranslateResult translate(uint64_t vaddr, bool write, bool instruction, bool user,
                                            uint64_t cr3, uint64_t cr0, uint64_t efer);

    /**
     * @brief Resolves a permitted TLB hit without walking or raising faults.
     *
     * @param[in] vaddr Virtual address.
     * @param[in] write Store access.
     * @param[in] instruction Instruction fetch.
     * @param[in] user CPL=3 access.
     * @param[in] cr0 Current CR0 (WP permission rule).
     * @param[in] efer Current EFER (NXE permission rule).
     * @param[out] paddr Physical address on success.
     * @return True for pass-through or a permitted hit; false needs translate().
     */
    [[nodiscard]] bool lookup(uint64_t vaddr, bool write, bool instruction, bool user, uint64_t cr0,
                              uint64_t efer, uint64_t& paddr) const {
        if (!paging_) {
            paddr = vaddr;
            return true;
        }
        const uint64_t vpn = vaddr >> 12U;
        const TlbEntry& entry = table_[vpn & (kEntries - 1U)];
        if (entry.flags == 0 || entry.tag != vpn || (user && (entry.flags & kUser) == 0) ||
            (write && (entry.flags & kWrite) == 0 && (user || (cr0 & (1ULL << 16U)) != 0)) ||
            (instruction && (efer & (1ULL << 11U)) != 0 && (entry.flags & kNx) != 0)) {
            return false;
        }
        paddr = entry.frame + (vaddr & 0xFFFULL);
        return true;
    }

private:
    /// One 4 KiB page-table entry.
    struct Walk {
        uint64_t paddr = 0;
        uint64_t flags = 0;
        bool ok = false;
        uint64_t error = 0;
    };
    /// Four-level walk (with 2 MiB / 1 GiB large-page support).
    Walk walk(uint64_t vaddr, bool write, bool instruction, bool user, uint64_t cr3, uint64_t cr0,
              uint64_t efer);
    /// Reads one 8-byte page-table entry at a physical address.
    bool read_pte(uint64_t paddr, uint64_t& value);
    /// Writes one 8-byte page-table entry at a physical address.
    void write_pte(uint64_t paddr, uint64_t value);

    kernel::Bus& bus_;
    bool paging_ = false;
    std::array<TlbEntry, kEntries> table_{};
};

} // namespace micro_forge::arch::x86_64
