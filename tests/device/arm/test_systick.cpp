#include <gtest/gtest.h>
#include <memory>

#include "micro_forge/device/arm/systick.hpp"
#include "micro_forge/device/base/types.hpp"

using namespace micro_forge::device;
using namespace micro_forge::device::arm;
using namespace micro_forge::device;

class SysTickTest : public ::testing::Test {
protected:
    std::unique_ptr<SysTickPeripheral> systick_;

    void SetUp() override { systick_ = std::make_unique<SysTickPeripheral>(); }
};

TEST_F(SysTickTest, CountDownToZero) {
    ASSERT_TRUE(systick_->write(0x04U, 100U, Width::kWord).has_value()); // LOAD
    ASSERT_TRUE(systick_->write(0x00U, 0x1U, Width::kWord)
                    .has_value()); // ENABLE, reload occurs on next input tick

    systick_->tick(101U);

    auto ctrl = systick_->read(0x00U, Width::kWord);
    ASSERT_TRUE(ctrl.has_value());
    EXPECT_TRUE(*ctrl & (1U << 16U)); // COUNTFLAG set

    auto val = systick_->read(0x08U, Width::kWord);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0U); // Reload consumes the next tick
}

TEST_F(SysTickTest, CountDownPartial) {
    ASSERT_TRUE(systick_->write(0x04U, 100U, Width::kWord).has_value()); // LOAD
    ASSERT_TRUE(systick_->write(0x00U, 0x1U, Width::kWord)
                    .has_value()); // ENABLE, reload occurs on next input tick

    systick_->tick(50U);

    auto ctrl = systick_->read(0x00U, Width::kWord);
    ASSERT_TRUE(ctrl.has_value());
    EXPECT_FALSE(*ctrl & (1U << 16U)); // COUNTFLAG not set

    auto val = systick_->read(0x08U, Width::kWord);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 51U); // One tick reloads, then 49 ticks decrement
}

TEST_F(SysTickTest, TickIntTriggersCallback) {
    bool callback_fired = false;
    systick_->set_irq_callback([&]() { callback_fired = true; });

    ASSERT_TRUE(systick_->write(0x04U, 100U, Width::kWord).has_value());
    ASSERT_TRUE(systick_->write(0x08U, 100U, Width::kWord).has_value());
    ASSERT_TRUE(systick_->write(0x00U, 0x3U, Width::kWord).has_value()); // ENABLE + TICKINT

    systick_->tick(101U);

    EXPECT_TRUE(callback_fired);
}

TEST_F(SysTickTest, MultipleZeroCrossings) {
    int callback_count = 0U;
    systick_->set_irq_callback([&]() { callback_count++; });

    ASSERT_TRUE(systick_->write(0x04U, 10U, Width::kWord).has_value());
    ASSERT_TRUE(systick_->write(0x08U, 10U, Width::kWord).has_value());
    ASSERT_TRUE(systick_->write(0x00U, 0x3U, Width::kWord).has_value());

    // Three periods of 11 ticks, then one reload and one decrement.
    systick_->tick(35U);

    EXPECT_EQ(callback_count, 3U);

    auto ctrl = systick_->read(0x00U, Width::kWord);
    ASSERT_TRUE(ctrl.has_value());
    EXPECT_TRUE(*ctrl & (1U << 16U)); // COUNTFLAG set

    auto val = systick_->read(0x08U, Width::kWord);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 9U); // 35 = 3 * 11 + reload + decrement
}

TEST_F(SysTickTest, DisabledNoDecrement) {
    ASSERT_TRUE(systick_->write(0x04U, 100U, Width::kWord).has_value()); // LOAD
    ASSERT_TRUE(systick_->write(0x00U, 0x1U, Width::kWord)
                    .has_value()); // ENABLE, reload occurs on next input tick
    systick_->tick(1U);            // Load VAL=100 before disabling.
    ASSERT_TRUE(systick_->write(0x00U, 0x0U, Width::kWord).has_value()); // Disable

    systick_->tick(101U);

    auto val = systick_->read(0x08U, Width::kWord);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 100U); // Unchanged — disabled
}

TEST_F(SysTickTest, WriteValClears) {
    ASSERT_TRUE(systick_->write(0x00U, 0x1U, Width::kWord).has_value());
    ASSERT_TRUE(systick_->write(0x08U, 999U, Width::kWord).has_value());

    auto val = systick_->read(0x08U, Width::kWord);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0U);
}

TEST_F(SysTickTest, LoadMasksTo24Bits) {
    ASSERT_TRUE(systick_->write(0x04U, 0x01FFFFFFU, Width::kWord).has_value());

    auto load = systick_->read(0x04U, Width::kWord);
    ASSERT_TRUE(load.has_value());
    EXPECT_EQ(*load, 0x00FFFFFFU); // Masked to 24 bits
}

TEST_F(SysTickTest, CountFlagIsReadClearAndNotWritable) {
    ASSERT_TRUE(systick_->write(0x04U, 1U, Width::kWord).has_value());
    ASSERT_TRUE(systick_->write(0x00U, 0xFFFFFFFFU, Width::kWord).has_value());
    EXPECT_EQ(systick_->read(0x00U, Width::kWord).value(), 7U);
    systick_->tick(2U);
    ASSERT_TRUE(systick_->write(0x00U, 1U, Width::kWord).has_value());
    EXPECT_EQ(systick_->read(0x00U, Width::kWord).value(), (1U << 16U) | 1U);
    EXPECT_EQ(systick_->read(0x00U, Width::kWord).value(), 1U);
    systick_->tick(2U);
    ASSERT_TRUE(systick_->write(0x08U, 0xFFFFFFFFU, Width::kWord).has_value());
    EXPECT_EQ(systick_->read(0x08U, Width::kWord).value(), 0U);
    EXPECT_EQ(systick_->read(0x00U, Width::kWord).value(), 1U);
}

TEST_F(SysTickTest, ReloadConsumesOneCycleAndNextEventIncludesIt) {
    unsigned irqs = 0U;
    systick_->set_irq_callback([&] { ++irqs; });
    ASSERT_TRUE(systick_->write(0x04U, 2U, Width::kWord).has_value());
    ASSERT_TRUE(systick_->write(0x00U, 3U, Width::kWord).has_value());
    EXPECT_EQ(systick_->val(), 0U);
    EXPECT_EQ(systick_->cycles_until_next_tick(), 3U);
    systick_->tick(1U);
    EXPECT_EQ(systick_->val(), 2U);
    EXPECT_EQ(systick_->cycles_until_next_event(), 2U);
    EXPECT_EQ(irqs, 0U);
    systick_->tick(1U);
    EXPECT_EQ(systick_->val(), 1U);
    EXPECT_EQ(irqs, 0U);
    systick_->tick(1U);
    EXPECT_EQ(systick_->val(), 0U);
    EXPECT_EQ(irqs, 1U);
    EXPECT_EQ(systick_->cycles_until_next_event(), 3U);
}

TEST_F(SysTickTest, ZeroReloadDoesNotGenerateInterrupts) {
    unsigned irqs = 0U;
    systick_->set_irq_callback([&] { ++irqs; });
    ASSERT_TRUE(systick_->write(0x00U, 3U, Width::kWord).has_value());
    systick_->tick(1000U);
    EXPECT_EQ(irqs, 0U);
    EXPECT_EQ(systick_->val(), 0U);
    EXPECT_EQ(systick_->ctrl(), 3U);
    EXPECT_EQ(systick_->cycles_until_next_tick(), 0U);
}
