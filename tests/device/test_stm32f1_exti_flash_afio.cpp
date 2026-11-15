#include <gtest/gtest.h>

#include "micro_forge/device/base/types.hpp"
#include "micro_forge/device/stm32f1/stm32f1_afio.hpp"
#include "micro_forge/device/stm32f1/stm32f1_exti.hpp"
#include "micro_forge/device/stm32f1/stm32f1_flash.hpp"

using namespace micro_forge::device;
using namespace micro_forge::device::stm32f1;

// ── EXTI Tests ──

TEST(ExtiTest, RegisterReadWrite) {
    Stm32f1Exti exti;
    ASSERT_TRUE(exti.write(0x00U, 0x100U, Width::kWord).has_value()); // IMR
    ASSERT_TRUE(exti.write(0x08U, 0x200U, Width::kWord).has_value()); // RTSR
    auto imr = exti.read(0x00U, Width::kWord);
    auto rtsr = exti.read(0x08U, Width::kWord);
    ASSERT_TRUE(imr.has_value() && rtsr.has_value());
    EXPECT_EQ(*imr, 0x100U);
    EXPECT_EQ(*rtsr, 0x200U);
}

TEST(ExtiTest, PendingClearsOnWrite1) {
    Stm32f1Exti exti;
    ASSERT_TRUE(exti.write(0x00U, 1U, Width::kWord).has_value()); // IMR line0
    ASSERT_TRUE(exti.write(0x10U, 1U, Width::kWord).has_value()); // SWIER line0
    EXPECT_TRUE(exti.pending(0U));
    ASSERT_TRUE(exti.write(0x14U, 1U, Width::kWord).has_value()); // PR w1c
    EXPECT_FALSE(exti.pending(0U));
}

TEST(ExtiTest, RisingEdgeOnRoutedPortTriggers) {
    Stm32f1Afio afio;
    Stm32f1Exti exti;
    exti.set_afio(afio);
    intr::intr_n_t raised = 0xFFU;
    exti.set_irq_callback([&](intr::intr_n_t irq) { raised = irq; });
    ASSERT_TRUE(afio.write(0x08U, 0U, Width::kWord).has_value());       // EXTICR1 → PA
    ASSERT_TRUE(exti.write(0x00U, 1U << 3U, Width::kWord).has_value()); // IMR line3
    ASSERT_TRUE(exti.write(0x08U, 1U << 3U, Width::kWord).has_value()); // RTSR line3
    exti.on_gpio_edge({.port = 'A', .pin = 3U, .rising = true});
    EXPECT_TRUE(exti.pending(3U));
    EXPECT_EQ(raised, 9U); // EXTI3 → IRQ9
}

TEST(ExtiTest, EdgeOnWrongPortDoesNotTrigger) {
    Stm32f1Afio afio;
    Stm32f1Exti exti;
    exti.set_afio(afio);
    bool fired = false;
    exti.set_irq_callback([&](intr::intr_n_t) { fired = true; });
    ASSERT_TRUE(afio.write(0x08U, 0U, Width::kWord).has_value());       // line3 → PA
    ASSERT_TRUE(exti.write(0x00U, 1U << 3U, Width::kWord).has_value()); // IMR
    ASSERT_TRUE(exti.write(0x08U, 1U << 3U, Width::kWord).has_value()); // RTSR
    exti.on_gpio_edge(
        {.port = 'B', .pin = 3U, .rising = true}); // PB3 but routed to PA → no trigger
    EXPECT_FALSE(fired);
    EXPECT_FALSE(exti.pending(3U));
}

TEST(ExtiTest, ImrMasksLine) {
    Stm32f1Afio afio;
    Stm32f1Exti exti;
    exti.set_afio(afio);
    bool fired = false;
    exti.set_irq_callback([&](intr::intr_n_t) { fired = true; });
    ASSERT_TRUE(afio.write(0x08U, 0U, Width::kWord).has_value());
    ASSERT_TRUE(exti.write(0x08U, 1U << 3U, Width::kWord).has_value()); // RTSR line3
    // IMR line3 NOT set
    exti.on_gpio_edge({.port = 'A', .pin = 3U, .rising = true});
    EXPECT_FALSE(fired);
    EXPECT_FALSE(exti.pending(3U));
}

TEST(ExtiTest, FallingOnlyDoesNotTriggerOnRising) {
    Stm32f1Afio afio;
    Stm32f1Exti exti;
    exti.set_afio(afio);
    int count = 0U;
    exti.set_irq_callback([&](intr::intr_n_t) { ++count; });
    ASSERT_TRUE(afio.write(0x08U, 0U, Width::kWord).has_value());
    ASSERT_TRUE(exti.write(0x00U, 1U << 3U, Width::kWord).has_value()); // IMR
    ASSERT_TRUE(exti.write(0x0CU, 1U << 3U, Width::kWord).has_value()); // FTSR only
    exti.on_gpio_edge({.port = 'A', .pin = 3U, .rising = true}); // rising, only falling armed
    EXPECT_EQ(count, 0U);
    exti.on_gpio_edge({.port = 'A', .pin = 3U, .rising = false}); // falling → triggers
    EXPECT_EQ(count, 1U);
}

// ── FLASH Tests ──

TEST(FlashTest, AcrDefault) {
    Stm32f1Flash flash;
    auto acr = flash.read(0x00U, Width::kWord);
    ASSERT_TRUE(acr.has_value());
    EXPECT_EQ(*acr, 0x00000030U); // PRFTBE=1, LATENCY=1
}

TEST(FlashTest, AcrWriteRead) {
    Stm32f1Flash flash;
    ASSERT_TRUE(flash.write(0x00U, 0x00000002U, Width::kWord).has_value()); // LATENCY=2
    auto acr = flash.read(0x00U, Width::kWord);
    ASSERT_TRUE(acr.has_value());
    EXPECT_EQ(*acr, 0x00000002U);
}

TEST(FlashTest, KeyrWriteAccepted) {
    Stm32f1Flash flash;
    ASSERT_TRUE(flash.write(0x04U, 0x45670123U, Width::kWord).has_value());
    auto keyr = flash.read(0x04U, Width::kWord);
    ASSERT_TRUE(keyr.has_value());
    EXPECT_EQ(*keyr, 0x45670123U);
}

TEST(FlashTest, SrNotBusy) {
    Stm32f1Flash flash;
    auto sr = flash.read(0x0CU, Width::kWord);
    ASSERT_TRUE(sr.has_value());
    EXPECT_EQ(*sr, 0U); // BSY=0
}

TEST(FlashTest, CrDefault) {
    Stm32f1Flash flash;
    auto cr = flash.read(0x10U, Width::kWord);
    ASSERT_TRUE(cr.has_value());
    EXPECT_TRUE(*cr & (1U << 7U)); // LOCK bit set after reset
}

TEST(FlashTest, ReservedOffsetReturnsZero) {
    Stm32f1Flash flash;
    auto val = flash.read(0x50U, Width::kWord);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0U);
}

TEST(FlashTest, ReservedWriteNoFault) {
    Stm32f1Flash flash;
    EXPECT_TRUE(flash.write(0x50U, 0xDEADU, Width::kWord).has_value());
}

// ── AFIO Tests ──

TEST(AfioTest, MaprDefault) {
    Stm32f1Afio afio;
    auto mapr = afio.read(0x04U, Width::kWord);
    ASSERT_TRUE(mapr.has_value());
    EXPECT_EQ(*mapr, 0U);
}

TEST(AfioTest, MaprWriteRead) {
    Stm32f1Afio afio;
    ASSERT_TRUE(afio.write(0x04U, 0x02000000U, Width::kWord).has_value()); // SWJ config
    auto mapr = afio.read(0x04U, Width::kWord);
    ASSERT_TRUE(mapr.has_value());
    EXPECT_EQ(*mapr, 0x02000000U);
}

TEST(AfioTest, ExticrWriteRead) {
    Stm32f1Afio afio;
    // EXTICR1: map PA0-PA3 as EXTI sources
    ASSERT_TRUE(afio.write(0x08U, 0x00000000U, Width::kWord).has_value());
    auto exticr1 = afio.read(0x08U, Width::kWord);
    ASSERT_TRUE(exticr1.has_value());
    EXPECT_EQ(*exticr1, 0U);

    // EXTICR2: map PB4-PB7
    ASSERT_TRUE(afio.write(0x0CU, 0x00001111U, Width::kWord).has_value());
    auto exticr2 = afio.read(0x0CU, Width::kWord);
    ASSERT_TRUE(exticr2.has_value());
    EXPECT_EQ(*exticr2, 0x00001111U);
}

TEST(AfioTest, ExticrAllFour) {
    Stm32f1Afio afio;
    for (int i = 0U; i < 4U; ++i) {
        addr_t const offset = 0x08U + (i * 4U);
        data_t val = static_cast<data_t>(i + 1U) * 0x11111111U;
        ASSERT_TRUE(afio.write(offset, val, Width::kWord).has_value());
        auto r = afio.read(offset, Width::kWord);
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(*r, val);
    }
}

TEST(AfioTest, EvcrWriteRead) {
    Stm32f1Afio afio;
    ASSERT_TRUE(afio.write(0x00U, 0x000000C0U, Width::kWord).has_value());
    auto evcr = afio.read(0x00U, Width::kWord);
    ASSERT_TRUE(evcr.has_value());
    EXPECT_EQ(*evcr, 0x000000C0U);
}

TEST(AfioTest, Mapr2WriteRead) {
    Stm32f1Afio afio;
    ASSERT_TRUE(afio.write(0x18U, 0x00000001U, Width::kWord).has_value());
    auto mapr2 = afio.read(0x18U, Width::kWord);
    ASSERT_TRUE(mapr2.has_value());
    EXPECT_EQ(*mapr2, 0x00000001U);
}

TEST(AfioTest, RegisterAccess) {
    Stm32f1Afio afio;

    ASSERT_TRUE(afio.write(0x4U, 0x02000000U, Width::kWord).has_value());
    auto val = afio.read(0x4U, Width::kWord);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0x02000000U);
}

TEST(AfioTest, ReservedOffsetIsCompatibilityNoOp) {
    Stm32f1Afio afio;

    auto val = afio.read(0x50U, Width::kWord);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0U);
    EXPECT_TRUE(afio.write(0x50U, 0xDEADBEEFU, Width::kWord).has_value());
}
