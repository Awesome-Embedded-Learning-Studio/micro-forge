/**
 * @file test_mmu.cpp
 * @brief Batch-6a software MMU: identity page tables built by hand in
 *        guest RAM, 2 MiB large pages, #PF error codes and CR2.
 */

#include "cpu_fixture.hpp"

#include <micro_forge/arch/x86_64/mmu.hpp>

using micro_forge::arch::x86_64::Mmu;

namespace {

constexpr uint64_t kPml4 = 0x400000;
constexpr uint64_t kPdpt = 0x401000;
constexpr uint64_t kPd = 0x402000;
constexpr uint64_t kMt = 0x403000; // 4 KiB page table for the 0..2 MiB range

class MmuTest : public CpuTest {
protected:
    void write64(uint64_t address, uint64_t value) {
        for (unsigned i = 0; i < 8; ++i) {
            ram_.data()[address + i] = static_cast<uint8_t>(value >> (8U * i));
        }
    }

    /// Identity-maps [0, 8 MiB) with 2 MiB large pages, plus a 4 KiB
    /// table at [8 MiB, 8 MiB + 4 KiB) when @p fine_map is set.
    void install_identity(bool fine_map = false) {
        write64(kPml4, kPdpt | Mmu::kPresent | Mmu::kWrite | Mmu::kUser);
        write64(kPdpt, kPd | Mmu::kPresent | Mmu::kWrite | Mmu::kUser);
        for (uint64_t i = 0; i < 4; ++i) {
            write64(kPd + (i * 8U), (i * 0x200000ULL) | Mmu::kPresent | Mmu::kWrite |
                                        Mmu::kPageSize | Mmu::kAccessed | Mmu::kDirty);
        }
        if (fine_map) {
            write64(kPd + (uint64_t{4} * 8U), kMt | Mmu::kPresent | Mmu::kWrite | Mmu::kUser);
            for (uint64_t i = 0; i < 2; ++i) {
                write64(kMt + (i * 8U), (0x800000ULL + (i * 0x1000ULL)) | Mmu::kPresent |
                                            Mmu::kWrite | Mmu::kUser | Mmu::kAccessed);
            }
        }
        cpu_.set_control_register(2, kPml4); // CR3
        cpu_.flush_tlb();
        cpu_.set_paging(true);
    }
};

TEST_F(MmuTest, IdentityMapServesDataAndFetch) {
    // mov eax, [0x2000]; and the instruction fetch itself walk the map.
    // mov eax, [0x2000] (absolute: SIB form, not RIP-relative).
    load({0x8B, 0x04, 0x25, 0x00, 0x20, 0x00, 0x00});
    poke(0x2000, {0x34, 0x12, 0x00, 0x00});
    install_identity();
    (void)cpu_.run(10);
    EXPECT_EQ(ExitStatus::Kind::kHalt, cpu_.exit_status().kind);
    EXPECT_EQ(0x1234U, static_cast<uint32_t>(cpu_.gpr(Reg::kRax)));
}

TEST_F(MmuTest, UnmappedFetchFaultsWithCr2) {
    // Jump outside the mapped window: fetch faults and CR2 names the rip.
    load({0xE9, 0x00, 0x00, 0x00, 0x10}); // jmp +0x10000000
    install_identity();
    (void)cpu_.run(10);
    EXPECT_EQ(ExitStatus::Kind::kFault, cpu_.exit_status().kind);
    EXPECT_EQ(static_cast<uint64_t>(FaultId::kFetch), cpu_.exit_status().detail);
    EXPECT_EQ(kCodeBase + 5U + 0x10000000ULL, cpu_.page_fault_address());
    EXPECT_EQ(0U, cpu_.page_fault_error()); // present=0, read, supervisor
}

TEST_F(MmuTest, WriteToReadOnlyPageFaultsWhenWpSet) {
    // mov [0x2000], eax with the page marked read-only; CR0.WP=1 makes
    // the supervisor store fault.
    load({0x89, 0x04, 0x25, 0x00, 0x20, 0x00, 0x00}); // mov [0x2000], eax
    cpu_.set_gpr(Reg::kRax, 0x7777);
    // Rebuild the PDPT/PD without write permission on the first page.
    install_identity();
    write64(kPd, Mmu::kPresent | Mmu::kPageSize | Mmu::kAccessed); // clear RW
    cpu_.flush_tlb();
    cpu_.set_control_register(0, 0x80000001ULL | (1ULL << 16U)); // WP
    (void)cpu_.run(10);
    EXPECT_EQ(ExitStatus::Kind::kFault, cpu_.exit_status().kind);
    EXPECT_EQ(static_cast<uint64_t>(FaultId::kMemory), cpu_.exit_status().detail);
    EXPECT_TRUE((cpu_.page_fault_error() & Mmu::kErrWrite) != 0);
}

TEST_F(MmuTest, SupervisorWriteToReadOnlyPagePassesWithoutWp) {
    load({0x89, 0x04, 0x25, 0x00, 0x20, 0x00, 0x00}); // mov [0x2000], eax
    cpu_.set_gpr(Reg::kRax, 0x7777);
    install_identity();
    write64(kPd, Mmu::kPresent | Mmu::kPageSize | Mmu::kAccessed);
    cpu_.flush_tlb();
    cpu_.set_control_register(0, 0x80000001ULL); // WP clear
    (void)cpu_.run(10);
    EXPECT_EQ(ExitStatus::Kind::kHalt, cpu_.exit_status().kind);
    EXPECT_EQ(0x77U, ram_.data()[0x2000]);
}

TEST_F(MmuTest, NxPageRefusesFetchOnly) {
    // Map the code page NX with NXE enabled: the fetch faults, and the
    // same address is still readable as data.
    load({0x90}); // nop at 0x100000
    install_identity();
    // Map [0, 2 MiB) to itself, marked NX: the fetch faults, data is fine.
    write64(kPd, Mmu::kPresent | Mmu::kPageSize | Mmu::kAccessed | Mmu::kNx);
    cpu_.flush_tlb();
    cpu_.set_control_register(0, 0x80000001ULL);           // no WP
    ASSERT_TRUE(cpu_.write_msr(0xC0000080U, 1ULL << 11U)); // EFER.NXE
    (void)cpu_.run(10);
    EXPECT_EQ(ExitStatus::Kind::kFault, cpu_.exit_status().kind);
    EXPECT_TRUE((cpu_.page_fault_error() & Mmu::kErrInstruction) != 0);
}

TEST_F(MmuTest, InvalidatedEntryWalksAgain) {
    load({0x8B, 0x04, 0x25, 0x00, 0x20, 0x00, 0x00}); // mov eax, [0x2000]
    poke(0x2000, {0xAA, 0x00, 0x00, 0x00});
    install_identity();
    (void)cpu_.run(10); // warm the TLB
    EXPECT_EQ(0xAAU, static_cast<uint32_t>(cpu_.gpr(Reg::kRax)));

    // Change the mapping under the cached entry, drop it, run again.
    write64(kPd, 0x200000ULL | Mmu::kPresent | Mmu::kPageSize | Mmu::kAccessed); // map 0 -> 2MB
    poke(0x200000 + 0x2000, {0xBB, 0x00, 0x00, 0x00});
    cpu_.invalidate_page(0x2000);
    cpu_.set_gpr(Reg::kRax, 0);
    cpu_.reset(kCodeBase, kStackTop);
    cpu_.set_control_register(2, kPml4);
    cpu_.set_paging(true);
    (void)cpu_.run(10);
    EXPECT_EQ(0xBBU, static_cast<uint32_t>(cpu_.gpr(Reg::kRax)));
}

TEST_F(MmuTest, FastLookupRechecksCurrentPermissions) {
    install_identity();
    write64(kPd, Mmu::kPresent | Mmu::kPageSize | Mmu::kAccessed | Mmu::kNx);
    Mmu mmu(bus_);
    mmu.set_paging(true);
    uint64_t paddr = 0;
    constexpr uint64_t nxe = 1ULL << 11U;
    constexpr uint64_t wp = 1ULL << 16U;
    EXPECT_FALSE(mmu.lookup(0x2000, false, false, false, 0, nxe, paddr));
    ASSERT_TRUE(mmu.translate(0x2000, false, false, false, kPml4, 0, nxe).ok);
    ASSERT_TRUE(mmu.lookup(0x2000, false, false, false, 0, nxe, paddr));
    EXPECT_EQ(0x2000U, paddr);
    EXPECT_TRUE(mmu.lookup(0x2000, true, false, false, 0, nxe, paddr));
    EXPECT_FALSE(mmu.lookup(0x2000, true, false, false, wp, nxe, paddr));
    EXPECT_FALSE(mmu.lookup(0x2000, false, false, true, 0, nxe, paddr));
    EXPECT_FALSE(mmu.lookup(0x2000, false, true, false, 0, nxe, paddr));
    // Permission failures use the regular translation path for error codes.
    EXPECT_FALSE(mmu.translate(0x2000, true, false, false, kPml4, wp, nxe).ok);
    EXPECT_FALSE(mmu.translate(0x2000, false, true, false, kPml4, 0, nxe).ok);
}

TEST_F(MmuTest, FastLookupHonorsInvalidationFlushAndPagingToggle) {
    install_identity();
    Mmu mmu(bus_);
    mmu.set_paging(true);
    uint64_t paddr = 0;
    ASSERT_TRUE(mmu.translate(0x2000, false, false, false, kPml4, 0, 0).ok);
    write64(kPd, 0x200000ULL | Mmu::kPresent | Mmu::kPageSize | Mmu::kAccessed);
    ASSERT_TRUE(mmu.lookup(0x2000, false, false, false, 0, 0, paddr));
    EXPECT_EQ(0x2000U, paddr); // old mapping until INVLPG or CR3 flush
    mmu.invalidate(0x2000);
    EXPECT_FALSE(mmu.lookup(0x2000, false, false, false, 0, 0, paddr));
    ASSERT_TRUE(mmu.translate(0x2000, false, false, false, kPml4, 0, 0).ok);
    ASSERT_TRUE(mmu.lookup(0x2000, false, false, false, 0, 0, paddr));
    EXPECT_EQ(0x202000U, paddr);
    mmu.flush();
    EXPECT_FALSE(mmu.lookup(0x2000, false, false, false, 0, 0, paddr));
    mmu.set_paging(false);
    ASSERT_TRUE(mmu.lookup(0x2000, true, true, true, 1ULL << 16U, 1ULL << 11U, paddr));
    EXPECT_EQ(0x2000U, paddr);
}

} // namespace
