#include <gtest/gtest.h>

#include "memory/bus.hpp"
#include "periph/nvic.hpp"

using namespace micro_forge;
using namespace micro_forge::periph;

// ── Direct API tests (no bus) ──

TEST(NvicTest, SetPendingNotEnabled) {
    NvicPeripheral nvic;
    nvic.set_pending(5);
    EXPECT_FALSE(nvic.is_enabled(5));
    EXPECT_FALSE(nvic.has_pending_irq()); // pending + !enabled = not active
}

TEST(NvicTest, EnableAndPending) {
    NvicPeripheral nvic;
    ASSERT_TRUE(
        nvic.write(0x000, (1u << 5), Width::Word).has_value()); // ISER0, bit 5
    nvic.set_pending(5);
    EXPECT_TRUE(nvic.has_pending_irq());
    EXPECT_TRUE(nvic.is_enabled(5));
}

TEST(NvicTest, HighestPendingReturnsLowest) {
    NvicPeripheral nvic;
    ASSERT_TRUE(
        nvic.write(0x000, (1u << 5) | (1u << 10), Width::Word).has_value());
    nvic.set_pending(10);
    nvic.set_pending(5);
    EXPECT_EQ(nvic.highest_pending_irq(), 5);
}

TEST(NvicTest, ClearPending) {
    NvicPeripheral nvic;
    ASSERT_TRUE(nvic.write(0x000, (1u << 5), Width::Word).has_value());
    nvic.set_pending(5);
    EXPECT_TRUE(nvic.has_pending_irq());

    nvic.clear_pending(5);
    EXPECT_FALSE(nvic.has_pending_irq());
}

TEST(NvicTest, DisabledIrqNotServiced) {
    NvicPeripheral nvic;
    nvic.set_pending(5);
    EXPECT_FALSE(nvic.is_enabled(5));
    EXPECT_FALSE(nvic.has_pending_irq()); // pending + !enabled = not active
}

TEST(NvicTest, InvalidIrqQueriesAreBounded) {
    NvicPeripheral nvic;

    ASSERT_TRUE(nvic.write(0x01C, 0x00008000u, Width::Word).has_value());
    EXPECT_TRUE(nvic.is_enabled(239));
    EXPECT_FALSE(nvic.is_enabled(240));
    EXPECT_FALSE(nvic.is_enabled(255));
    EXPECT_EQ(nvic.irq_priority(240), 0xFF);
}

// ── MMIO tests (through bus) ──

class NvicMmioTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto result =
            bus_.map(memory::region(0xE000E100, 0xC00, nvic_.GetWeak()));
        ASSERT_TRUE(result.has_value());
    }

    memory::Bus bus_;
    NvicPeripheral nvic_;
};

TEST_F(NvicMmioTest, IsenWriteRead) {
    ASSERT_TRUE(bus_.write(0xE000E100, (1u << 3), Width::Word).has_value());

    auto r = bus_.read(0xE000E100, Width::Word);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r & (1u << 3));
}

TEST_F(NvicMmioTest, IcerClearsIsen) {
    ASSERT_TRUE(bus_.write(0xE000E100, 0xFFFFFFFF, Width::Word).has_value());
    ASSERT_TRUE(bus_.write(0xE000E180, (1u << 3), Width::Word).has_value());

    auto r = bus_.read(0xE000E100, Width::Word);
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(*r & (1u << 3));
    EXPECT_TRUE(*r & (1u << 0));
}

TEST_F(NvicMmioTest, IsprWriteRead) {
    ASSERT_TRUE(bus_.write(0xE000E100, (1u << 7), Width::Word).has_value());
    ASSERT_TRUE(bus_.write(0xE000E200, (1u << 7), Width::Word).has_value());

    auto r = bus_.read(0xE000E200, Width::Word);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r & (1u << 7));

    EXPECT_TRUE(nvic_.has_pending_irq());
    EXPECT_EQ(nvic_.highest_pending_irq(), 7);
}

TEST_F(NvicMmioTest, IcprClearsIspr) {
    ASSERT_TRUE(bus_.write(0xE000E100, (1u << 7), Width::Word).has_value());
    ASSERT_TRUE(bus_.write(0xE000E200, (1u << 7), Width::Word).has_value());
    ASSERT_TRUE(bus_.write(0xE000E280, (1u << 7), Width::Word).has_value());

    EXPECT_FALSE(nvic_.has_pending_irq());
}

TEST_F(NvicMmioTest, PriorityReadWrite) {
    ASSERT_TRUE(bus_.write(0xE000E400,
                           0x20 | (0x40 << 8) | (0x60 << 16) | (0x80 << 24),
                           Width::Word)
                    .has_value());

    EXPECT_EQ(nvic_.irq_priority(0), 0x20);
    EXPECT_EQ(nvic_.irq_priority(1), 0x40);
    EXPECT_EQ(nvic_.irq_priority(2), 0x60);
    EXPECT_EQ(nvic_.irq_priority(3), 0x80);
}

// ── Width / decode error paths (were uncovered) ──

TEST(NvicTest, ReadRejectsNonWordWidth) {
    NvicPeripheral nvic;
    EXPECT_FALSE(nvic.read(0x000, Width::Byte).has_value());
}

TEST(NvicTest, WriteRejectsNonWordWidth) {
    NvicPeripheral nvic;
    EXPECT_FALSE(nvic.write(0x000, 0u, Width::Byte).has_value());
}

TEST(NvicTest, ReadUnmappedGapBetweenRegions) {
    NvicPeripheral nvic;
    // 0x040 falls between ISER (0x00-0x1F) and ICER (0x80-0x9F).
    EXPECT_FALSE(nvic.read(0x040, Width::Word).has_value());
}

TEST(NvicTest, WriteUnmappedGapBetweenRegions) {
    NvicPeripheral nvic;
    EXPECT_FALSE(nvic.write(0x040, 0u, Width::Word).has_value());
}

TEST(NvicTest, ReadUnmappedPastPriorityRegion) {
    NvicPeripheral nvic;
    EXPECT_FALSE(nvic.read(0x500, Width::Word).has_value());
}

// ── ICER/ICPR read aliases (read-side mirrors of ISER/ISPR) ──

TEST(NvicTest, IcerReadAliasesIsen) {
    NvicPeripheral nvic;
    ASSERT_TRUE(nvic.write(0x000, (1u << 5), Width::Word).has_value());
    auto r = nvic.read(0x080, Width::Word); // ICER0 reads ISER
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r & (1u << 5));
}

TEST(NvicTest, IcprReadAliasesIspr) {
    NvicPeripheral nvic;
    ASSERT_TRUE(nvic.write(0x100, (1u << 7), Width::Word).has_value()); // ISPR0
    auto r = nvic.read(0x180, Width::Word); // ICPR0 reads ISPR
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r & (1u << 7));
}

// ── highest_priority_pending_irq (priority-weighted, cache-backed) ──

TEST(NvicTest, HighestPriorityPendingRespectsPriority) {
    NvicPeripheral nvic;
    ASSERT_TRUE(
        nvic.write(0x000, (1u << 5) | (1u << 10), Width::Word).has_value());
    // IRQ10 lives in priority word 0x308, byte slot 2 (shift 16).
    ASSERT_TRUE(nvic.write(0x308, 0x40u << 16, Width::Word).has_value());
    nvic.set_pending(5);
    nvic.set_pending(10);
    // IRQ5 default pri 0 < IRQ10 pri 0x40 → IRQ5 wins.
    EXPECT_EQ(nvic.highest_priority_pending_irq(), 5u);
    // Raise IRQ5 above IRQ10 → IRQ10 wins.
    ASSERT_TRUE(nvic.write(0x304, 0x80u << 8, Width::Word).has_value());
    EXPECT_EQ(nvic.highest_priority_pending_irq(), 10u);
}

TEST(NvicTest, HighestPriorityPendingEmptyReturns255) {
    NvicPeripheral nvic;
    EXPECT_EQ(nvic.highest_priority_pending_irq(), 0xFFu);
}

TEST(NvicTest, HighestPriorityPendingCachedOnNoChange) {
    NvicPeripheral nvic;
    ASSERT_TRUE(nvic.write(0x000, (1u << 5), Width::Word).has_value());
    nvic.set_pending(5);
    EXPECT_EQ(nvic.highest_priority_pending_irq(), 5u);
    // Second call hits the cache (no NVIC state change in between).
    EXPECT_EQ(nvic.highest_priority_pending_irq(), 5u);
}
