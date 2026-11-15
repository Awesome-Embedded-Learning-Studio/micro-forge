#include <gtest/gtest.h>

#include "micro_forge/device/arm/nvic.hpp"
#include "micro_forge/device/base/device.hpp"
#include "micro_forge/device/base/types.hpp"
#include "micro_forge/device/base/weak_ptr.hpp"

using namespace micro_forge::device;
using namespace micro_forge::device::arm;
using namespace micro_forge::device;

// ── Direct API tests (no bus) ──

TEST(NvicTest, SetPendingNotEnabled) {
    NvicPeripheral nvic;
    nvic.set_pending(5U);
    EXPECT_FALSE(nvic.is_enabled(5U));
    EXPECT_FALSE(nvic.has_pending_irq()); // pending + !enabled = not active
}

TEST(NvicTest, EnableAndPending) {
    NvicPeripheral nvic;
    ASSERT_TRUE(nvic.write(0x000U, (1U << 5U), Width::kWord).has_value()); // ISER0, bit 5
    nvic.set_pending(5U);
    EXPECT_TRUE(nvic.has_pending_irq());
    EXPECT_TRUE(nvic.is_enabled(5U));
}

TEST(NvicTest, HighestPendingReturnsLowest) {
    NvicPeripheral nvic;
    ASSERT_TRUE(nvic.write(0x000U, (1U << 5U) | (1U << 10U), Width::kWord).has_value());
    nvic.set_pending(10U);
    nvic.set_pending(5U);
    EXPECT_EQ(nvic.highest_pending_irq(), 5U);
}

TEST(NvicTest, ClearPending) {
    NvicPeripheral nvic;
    ASSERT_TRUE(nvic.write(0x000U, (1U << 5U), Width::kWord).has_value());
    nvic.set_pending(5U);
    EXPECT_TRUE(nvic.has_pending_irq());

    nvic.clear_pending(5U);
    EXPECT_FALSE(nvic.has_pending_irq());
}

TEST(NvicTest, DisabledIrqNotServiced) {
    NvicPeripheral nvic;
    nvic.set_pending(5U);
    EXPECT_FALSE(nvic.is_enabled(5U));
    EXPECT_FALSE(nvic.has_pending_irq()); // pending + !enabled = not active
}

TEST(NvicTest, InvalidIrqQueriesAreBounded) {
    NvicPeripheral nvic;

    ASSERT_TRUE(nvic.write(0x01CU, 0x00008000U, Width::kWord).has_value());
    EXPECT_TRUE(nvic.is_enabled(239U));
    EXPECT_FALSE(nvic.is_enabled(240U));
    EXPECT_FALSE(nvic.is_enabled(255U));
    EXPECT_EQ(nvic.irq_priority(240U), 0xFFU);
}

// Register tests use device-relative offsets, independent of machine assembly.

class NvicRegisterTest : public ::testing::Test {
protected:
    NvicPeripheral nvic_;
};

TEST_F(NvicRegisterTest, IsenWriteRead) {
    ASSERT_TRUE(nvic_.write(0x0U, (1U << 3U), Width::kWord).has_value());

    auto r = nvic_.read(0x0U, Width::kWord);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r & (1U << 3U));
}

TEST_F(NvicRegisterTest, IcerClearsIsen) {
    ASSERT_TRUE(nvic_.write(0x0U, 0xFFFFFFFFU, Width::kWord).has_value());
    ASSERT_TRUE(nvic_.write(0x80U, (1U << 3U), Width::kWord).has_value());

    auto r = nvic_.read(0x0U, Width::kWord);
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(*r & (1U << 3U));
    EXPECT_TRUE(*r & (1U << 0U));
}

TEST_F(NvicRegisterTest, IsprWriteRead) {
    ASSERT_TRUE(nvic_.write(0x0U, (1U << 7U), Width::kWord).has_value());
    ASSERT_TRUE(nvic_.write(0x100U, (1U << 7U), Width::kWord).has_value());

    auto r = nvic_.read(0x100U, Width::kWord);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r & (1U << 7U));

    EXPECT_TRUE(nvic_.has_pending_irq());
    EXPECT_EQ(nvic_.highest_pending_irq(), 7U);
}

TEST_F(NvicRegisterTest, IcprClearsIspr) {
    ASSERT_TRUE(nvic_.write(0x0U, (1U << 7U), Width::kWord).has_value());
    ASSERT_TRUE(nvic_.write(0x100U, (1U << 7U), Width::kWord).has_value());
    ASSERT_TRUE(nvic_.write(0x180U, (1U << 7U), Width::kWord).has_value());

    EXPECT_FALSE(nvic_.has_pending_irq());
}

TEST_F(NvicRegisterTest, PriorityReadWrite) {
    ASSERT_TRUE(
        nvic_.write(0x300U, 0x20U | (0x40U << 8U) | (0x60U << 16U) | (0x80U << 24U), Width::kWord)
            .has_value());

    EXPECT_EQ(nvic_.irq_priority(0U), 0x20U);
    EXPECT_EQ(nvic_.irq_priority(1U), 0x40U);
    EXPECT_EQ(nvic_.irq_priority(2U), 0x60U);
    EXPECT_EQ(nvic_.irq_priority(3U), 0x80U);
}

// ── Width / decode error paths (were uncovered) ──

TEST(NvicTest, ReadRejectsNonWordWidth) {
    NvicPeripheral nvic;
    EXPECT_FALSE(nvic.read(0x000U, Width::kByte).has_value());
}

TEST(NvicTest, WriteRejectsNonWordWidth) {
    NvicPeripheral nvic;
    EXPECT_FALSE(nvic.write(0x000U, 0U, Width::kByte).has_value());
}

TEST(NvicTest, ReadUnmappedGapBetweenRegions) {
    NvicPeripheral nvic;
    // 0x040 falls between ISER (0x00-0x1F) and ICER (0x80-0x9F).
    EXPECT_FALSE(nvic.read(0x040U, Width::kWord).has_value());
}

TEST(NvicTest, WriteUnmappedGapBetweenRegions) {
    NvicPeripheral nvic;
    EXPECT_FALSE(nvic.write(0x040U, 0U, Width::kWord).has_value());
}

TEST(NvicTest, ReadUnmappedPastPriorityRegion) {
    NvicPeripheral nvic;
    EXPECT_FALSE(nvic.read(0x500U, Width::kWord).has_value());
}

// ── ICER/ICPR read aliases (read-side mirrors of ISER/ISPR) ──

TEST(NvicTest, IcerReadAliasesIsen) {
    NvicPeripheral nvic;
    ASSERT_TRUE(nvic.write(0x000U, (1U << 5U), Width::kWord).has_value());
    auto r = nvic.read(0x080U, Width::kWord); // ICER0 reads ISER
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r & (1U << 5U));
}

TEST(NvicTest, IcprReadAliasesIspr) {
    NvicPeripheral nvic;
    ASSERT_TRUE(nvic.write(0x100U, (1U << 7U), Width::kWord).has_value()); // ISPR0
    auto r = nvic.read(0x180U, Width::kWord);                              // ICPR0 reads ISPR
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r & (1U << 7U));
}

// ── highest_priority_pending_irq (priority-weighted, cache-backed) ──

TEST(NvicTest, HighestPriorityPendingRespectsPriority) {
    NvicPeripheral nvic;
    ASSERT_TRUE(nvic.write(0x000U, (1U << 5U) | (1U << 10U), Width::kWord).has_value());
    // IRQ10 lives in priority word 0x308, byte slot 2 (shift 16).
    ASSERT_TRUE(nvic.write(0x308U, 0x40U << 16U, Width::kWord).has_value());
    nvic.set_pending(5U);
    nvic.set_pending(10U);
    // IRQ5 default pri 0 < IRQ10 pri 0x40 → IRQ5 wins.
    EXPECT_EQ(nvic.highest_priority_pending_irq(), 5U);
    // Raise IRQ5 above IRQ10 → IRQ10 wins.
    ASSERT_TRUE(nvic.write(0x304U, 0x80U << 8U, Width::kWord).has_value());
    EXPECT_EQ(nvic.highest_priority_pending_irq(), 10U);
}

TEST(NvicTest, HighestPriorityPendingEmptyReturns255) {
    NvicPeripheral nvic;
    EXPECT_EQ(nvic.highest_priority_pending_irq(), 0xFFU);
}

TEST(NvicTest, HighestPriorityPendingCachedOnNoChange) {
    NvicPeripheral nvic;
    ASSERT_TRUE(nvic.write(0x000U, (1U << 5U), Width::kWord).has_value());
    nvic.set_pending(5U);
    EXPECT_EQ(nvic.highest_priority_pending_irq(), 5U);
    // Second call hits the cache (no NVIC state change in between).
    EXPECT_EQ(nvic.highest_priority_pending_irq(), 5U);
}

TEST(NvicTest, ByteAndHalfWordPriorityWritesPreserveAdjacentPriorities) {
    NvicPeripheral nvic;
    ASSERT_TRUE(nvic.write(0x300U, 0x40302010U, Width::kWord).has_value());
    ASSERT_TRUE(nvic.write(0x301U, 0xA0U, Width::kByte).has_value());
    EXPECT_EQ(nvic.read(0x300U, Width::kWord).value(), 0x4030A010U);
    ASSERT_TRUE(nvic.write(0x302U, 0xB0C0U, Width::kHalfWord).has_value());
    EXPECT_EQ(nvic.read(0x300U, Width::kWord).value(), 0xB0C0A010U);
}

TEST(NvicTest, DisableAndClearPendingInvalidatePriorityCache) {
    NvicPeripheral nvic;
    ASSERT_TRUE(nvic.write(0x000U, (1U << 5U) | (1U << 6U), Width::kWord).has_value());
    nvic.set_pending(5U);
    nvic.set_pending(6U);
    EXPECT_EQ(nvic.highest_priority_pending_irq(), 5U);
    ASSERT_TRUE(nvic.write(0x080U, 1U << 5U, Width::kWord).has_value());
    EXPECT_EQ(nvic.highest_priority_pending_irq(), 6U);
    ASSERT_TRUE(nvic.write(0x180U, 1U << 6U, Width::kWord).has_value());
    EXPECT_FALSE(nvic.has_pending_irq());
}

TEST(NvicTest, DeviceWeakObserverExpiresWithPeripheral) {
    WeakPtr<Device> observer;
    {
        NvicPeripheral nvic;
        observer = nvic.get_weak();
        ASSERT_TRUE(observer.is_valid());
        EXPECT_EQ(observer->name(), "NVIC");
        EXPECT_EQ(observer.get(), static_cast<Device*>(&nvic));
    }
    EXPECT_FALSE(observer.is_valid());
    EXPECT_EQ(observer.get(), nullptr);
    observer.reset();
    EXPECT_FALSE(observer.is_valid());
}
