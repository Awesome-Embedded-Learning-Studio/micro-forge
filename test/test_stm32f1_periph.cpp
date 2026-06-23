#include <gtest/gtest.h>

#include "chips/stm32f1/stm32f1_afio.hpp"
#include "chips/stm32f1/stm32f1_exti.hpp"
#include "chips/stm32f1/stm32f1_flash.hpp"
#include "chips/stm32f1/stm32f1_gpio.hpp"
#include "chips/stm32f1/stm32f1_rcc.hpp"
#include "chips/stm32f1/stm32f1_timer.hpp"
#include "chips/stm32f1/stm32f1_usart.hpp"
#include "memory/bus.hpp"
#include "memory/flat_memory.hpp"

#include <vector>

using namespace micro_forge;
using namespace micro_forge::chips::stm32f1;
using namespace micro_forge::memory;

// ── RCC Tests ──

TEST(RccTest, ReadResetValues) {
    Stm32f1Rcc rcc;
    auto cr = rcc.read(0x00, Width::Word);
    ASSERT_TRUE(cr.has_value());
    EXPECT_EQ(*cr, 0x00000083u);

    auto apb2 = rcc.read(0x18, Width::Word);
    ASSERT_TRUE(apb2.has_value());
    EXPECT_EQ(*apb2, 0u);
}

TEST(RccTest, WriteApb2Enable) {
    Stm32f1Rcc rcc;
    ASSERT_TRUE(rcc.write(0x18, 0x00000004, Width::Word).has_value());
    auto val = rcc.read(0x18, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0x00000004u);
}

TEST(RccTest, MmioThroughBus) {
    Bus bus;
    Stm32f1Rcc rcc;
    ASSERT_TRUE(bus.map(region(0x40021000, 0x400, rcc.GetWeak())).has_value());

    // Write APB2ENR via bus
    ASSERT_TRUE(bus.write(0x4002'1018, 0x00000004, Width::Word).has_value());
    auto val = bus.read(0x4002'1018, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0x00000004u);
}

TEST(RccTest, InvalidOffset) {
    Stm32f1Rcc rcc;
    auto r = rcc.read(0x20, Width::Word);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), BusError::PeripheralFault);
}

TEST(RccTest, ClockEnabledCheck) {
    Stm32f1Rcc rcc;
    EXPECT_FALSE(rcc.is_clock_enabled(0x40010800)); // GPIOA not enabled
    rcc.enable_clock(0x40010800);
    EXPECT_TRUE(rcc.is_clock_enabled(0x40010800));
}

TEST(RccTest, HsiDefaultReady) {
    Stm32f1Rcc rcc;
    auto cr = rcc.read(0x00, Width::Word);
    ASSERT_TRUE(cr.has_value());
    EXPECT_TRUE(*cr & (1u << 0));  // HSION
    EXPECT_TRUE(*cr & (1u << 1));  // HSIRDY
}

TEST(RccTest, HseOnSetsReady) {
    Stm32f1Rcc rcc;
    ASSERT_TRUE(rcc.write(0x00, 0x00010000, Width::Word).has_value()); // set HSEON
    auto cr = rcc.read(0x00, Width::Word);
    ASSERT_TRUE(cr.has_value());
    EXPECT_TRUE(*cr & (1u << 16)); // HSEON
    EXPECT_TRUE(*cr & (1u << 17)); // HSERDY
}

TEST(RccTest, PllOnSetsReady) {
    Stm32f1Rcc rcc;
    ASSERT_TRUE(rcc.write(0x00, 0x01000000, Width::Word).has_value()); // set PLLON
    auto cr = rcc.read(0x00, Width::Word);
    ASSERT_TRUE(cr.has_value());
    EXPECT_TRUE(*cr & (1u << 24)); // PLLON
    EXPECT_TRUE(*cr & (1u << 25)); // PLLRDY
}

TEST(RccTest, DisableClearsReady) {
    Stm32f1Rcc rcc;
    // Turn on HSE
    ASSERT_TRUE(rcc.write(0x00, 0x00010000, Width::Word).has_value());
    // Turn off HSE (clear HSEON)
    ASSERT_TRUE(rcc.write(0x00, 0x00000000, Width::Word).has_value());
    auto cr = rcc.read(0x00, Width::Word);
    ASSERT_TRUE(cr.has_value());
    EXPECT_FALSE(*cr & (1u << 16)); // HSEON off
    EXPECT_FALSE(*cr & (1u << 17)); // HSERDY off
}

TEST(RccTest, CfgrSwSyncsToSws) {
    Stm32f1Rcc rcc;
    // SW = 0b10 (HSE), expect SWS = 0b10
    ASSERT_TRUE(rcc.write(0x04, 0x00000002, Width::Word).has_value());
    auto cfgr = rcc.read(0x04, Width::Word);
    ASSERT_TRUE(cfgr.has_value());
    EXPECT_EQ((*cfgr >> 0) & 0x3, 0x2u); // SW = HSE
    EXPECT_EQ((*cfgr >> 2) & 0x3, 0x2u); // SWS = HSE
}

TEST(RccTest, CfgrSwPllSyncsToSws) {
    Stm32f1Rcc rcc;
    // SW = 0b11 (PLL)
    ASSERT_TRUE(rcc.write(0x04, 0x00000003, Width::Word).has_value());
    auto cfgr = rcc.read(0x04, Width::Word);
    ASSERT_TRUE(cfgr.has_value());
    EXPECT_EQ((*cfgr >> 2) & 0x3, 0x3u); // SWS follows SW
}

// ── GPIO Tests ──

TEST(GpioTest, WriteOdrAndRead) {
    Stm32f1Gpio gpio('A');
    ASSERT_TRUE(gpio.write(0x0C, 0x0020, Width::Word).has_value());
    auto val = gpio.read(0x0C, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0x0020u);
}

TEST(GpioTest, BsrrSetAndReset) {
    Stm32f1Gpio gpio('A');

    // Set pin 5
    ASSERT_TRUE(gpio.write(0x10, 0x00000020, Width::Word).has_value());
    auto odr = gpio.read(0x0C, Width::Word);
    ASSERT_TRUE(odr.has_value());
    EXPECT_TRUE((*odr >> 5) & 1);

    // Reset pin 5 via high 16 bits
    ASSERT_TRUE(gpio.write(0x10, 0x00200000, Width::Word).has_value());
    odr = gpio.read(0x0C, Width::Word);
    ASSERT_TRUE(odr.has_value());
    EXPECT_FALSE((*odr >> 5) & 1);
}

TEST(GpioTest, BsrrReadOnly) {
    Stm32f1Gpio gpio('A');
    auto r = gpio.read(0x10, Width::Word);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), BusError::PeripheralFault);
}

TEST(GpioTest, IdrReadOnly) {
    Stm32f1Gpio gpio('A');
    auto r = gpio.write(0x08, 0xFF, Width::Word);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), BusError::ReadOnly);
}

TEST(GpioTest, SimulateInput) {
    Stm32f1Gpio gpio('A');
    gpio.simulate_input(3, true);
    auto idr = gpio.read(0x08, Width::Word);
    ASSERT_TRUE(idr.has_value());
    EXPECT_TRUE((*idr >> 3) & 1);
}

TEST(GpioTest, PinChangeCallback) {
    Stm32f1Gpio gpio('A');
    uint8_t changed_pin = 0xFF;
    bool changed_high = false;
    gpio.set_pin_change_callback([&](uint8_t pin, bool high) {
        changed_pin = pin;
        changed_high = high;
    });

    ASSERT_TRUE(
        gpio.write(0x0C, 0x0020, Width::Word).has_value()); // pin 5 high
    EXPECT_EQ(changed_pin, 5);
    EXPECT_TRUE(changed_high);
}

TEST(GpioTest, SetPinInterface) {
    Stm32f1Gpio gpio('A');
    gpio.set_pin(3, true);
    EXPECT_TRUE(gpio.get_pin(3));
    gpio.set_pin(3, false);
    EXPECT_FALSE(gpio.get_pin(3));
}

TEST(GpioTest, ConfigurePin) {
    Stm32f1Gpio gpio('A');
    gpio.configure_pin(0, periph::PinMode::Output, periph::PinPull::None,
                       periph::PinSpeed::High);
    auto crl = gpio.read(0x00, Width::Word);
    ASSERT_TRUE(crl.has_value());
    // Pin 0 should have non-zero config (output 50MHz push-pull = 0x3)
    EXPECT_EQ(*crl & 0xF, 0x3u);
}

TEST(GpioTest, NameHandlesValidAndInvalidPorts) {
    EXPECT_EQ(Stm32f1Gpio('A').name(), "GPIOA");
    EXPECT_EQ(Stm32f1Gpio('E').name(), "GPIOE");
    EXPECT_EQ(Stm32f1Gpio('@').name(), "GPIO?");
    EXPECT_EQ(Stm32f1Gpio('F').name(), "GPIO?");
}

TEST(GpioTest, MmioThroughBus) {
    Bus bus;
    Stm32f1Gpio gpio('A');
    ASSERT_TRUE(bus.map(region(0x40010800, 0x400, gpio.GetWeak())).has_value());

    ASSERT_TRUE(bus.write(0x4001'0810, 0x00000020, Width::Word)
                    .has_value()); // BSRR set pin5
    auto odr = bus.read(0x4001'080C, Width::Word);
    ASSERT_TRUE(odr.has_value());
    EXPECT_TRUE((*odr >> 5) & 1);
}

// ── USART Tests ──

TEST(UsartTest, WriteDrOutputsChar) {
    Stm32f1Usart usart;
    uint8_t captured = 0;
    usart.set_output([&](uint8_t ch) { captured = ch; });

    ASSERT_TRUE(usart.write(0x04, 'H', Width::Word).has_value());
    EXPECT_EQ(captured, 'H');
}

TEST(UsartTest, SrTxeAlwaysHigh) {
    Stm32f1Usart usart;
    auto sr = usart.read(0x00, Width::Word);
    ASSERT_TRUE(sr.has_value());
    EXPECT_TRUE(*sr & 0x80);
}

TEST(UsartTest, SrTcAlwaysHigh) {
    Stm32f1Usart usart;
    auto sr = usart.read(0x00, Width::Word);
    ASSERT_TRUE(sr.has_value());
    EXPECT_TRUE(*sr & 0x40);
}

TEST(UsartTest, WriteCr1) {
    Stm32f1Usart usart;
    ASSERT_TRUE(usart.write(0x0C, 0x000C, Width::Word).has_value());
    auto cr1 = usart.read(0x0C, Width::Word);
    ASSERT_TRUE(cr1.has_value());
    EXPECT_EQ(*cr1, 0x000Cu);
}

TEST(UsartTest, CanSend) {
    Stm32f1Usart usart;
    EXPECT_TRUE(usart.can_send());
}

TEST(UsartTest, SendByteInterface) {
    Stm32f1Usart usart;
    uint8_t captured = 0;
    usart.set_output([&](uint8_t ch) { captured = ch; });
    usart.send_byte('X');
    EXPECT_EQ(captured, 'X');
}

TEST(UsartTest, MmioThroughBus) {
    Bus bus;
    Stm32f1Usart usart;
    ASSERT_TRUE(
        bus.map(region(0x40013800, 0x400, usart.GetWeak())).has_value());

    uint8_t captured = 0;
    usart.set_output([&](uint8_t ch) { captured = ch; });

    ASSERT_TRUE(bus.write(0x4001'3804, 'A', Width::Word).has_value());
    EXPECT_EQ(captured, 'A');
}

// ── Timer Tests ──

TEST(TimerTest, TickIncrements) {
    Stm32f1Timer tim;
    tim.set_auto_reload(100);
    tim.enable(true);
    tim.tick(10);
    EXPECT_EQ(tim.counter(), 10u);
}

TEST(TimerTest, Prescaler) {
    Stm32f1Timer tim;
    tim.set_auto_reload(100);
    tim.set_prescaler(9);
    tim.enable(true);
    tim.tick(100);
    EXPECT_EQ(tim.counter(), 10u);
}

TEST(TimerTest, PrescalerResidualAccumulatesAcrossSmallTicks) {
    Stm32f1Timer tim;
    tim.set_auto_reload(100);
    tim.set_prescaler(9);
    tim.enable(true);

    for (int i = 0; i < 100; ++i) {
        tim.tick(1);
    }

    EXPECT_EQ(tim.counter(), 10u);
}

TEST(TimerTest, OverflowSetsUif) {
    Stm32f1Timer tim;
    tim.set_auto_reload(10);
    tim.enable(true);
    tim.tick(10);
    EXPECT_TRUE(tim.update_flag());
    EXPECT_EQ(tim.counter(), 0u);
}

TEST(TimerTest, DisabledNoTick) {
    Stm32f1Timer tim;
    tim.set_auto_reload(100);
    tim.enable(false);
    tim.tick(100);
    EXPECT_EQ(tim.counter(), 0u);
}

TEST(TimerTest, TimerInterface) {
    Stm32f1Timer tim;
    tim.set_auto_reload(100);
    tim.set_prescaler(0);
    tim.enable(true);
    tim.tick(50);
    EXPECT_EQ(tim.counter(), 50u);
    EXPECT_FALSE(tim.update_flag());

    tim.tick(60);
    EXPECT_TRUE(tim.update_flag());
    tim.clear_update_flag();
    EXPECT_FALSE(tim.update_flag());
}

TEST(TimerTest, MmioThroughBus) {
    Bus bus;
    Stm32f1Timer tim;
    ASSERT_TRUE(bus.map(region(0x40000000, 0x400, tim.GetWeak())).has_value());

    ASSERT_TRUE(
        bus.write(0x4000'0000, 0x0001, Width::Word).has_value()); // CR1=CEN
    ASSERT_TRUE(bus.write(0x4000'002C, 100, Width::Word).has_value()); // ARR
    ASSERT_TRUE(bus.write(0x4000'0028, 0, Width::Word).has_value());   // PSC

    tim.tick(10);
    auto cnt = bus.read(0x4000'0034, Width::Word);
    ASSERT_TRUE(cnt.has_value());
    EXPECT_EQ(*cnt, 10u);
}

TEST(TimerTest, UifEdgeWithUieTriggersIrqCallback) {
    // UIF 0→1 edge + DIER.UIE → irq_callback fires exactly once; re-overflow
    // while UIF stays set does not re-fire (edge); clearing UIF re-arms.
    Stm32f1Timer tim;
    int count = 0;
    tim.set_irq_callback([&] { ++count; });
    tim.set_prescaler(0);     // divisor 1
    tim.set_auto_reload(5);
    ASSERT_TRUE(tim.write(0x0C, 1u, Width::Word).has_value()); // DIER.UIE
    tim.enable(true);         // CR1.CEN

    tim.tick(5);              // cnt 0→5 → overflow, UIF edge
    EXPECT_TRUE(tim.update_flag());
    EXPECT_EQ(count, 1);

    tim.tick(10);             // UIF still set → no re-fire (edge)
    EXPECT_EQ(count, 1);

    tim.clear_update_flag();  // re-arm
    tim.tick(5);
    EXPECT_EQ(count, 2);
}

TEST(TimerTest, UifWithoutUieDoesNotFireCallback) {
    Stm32f1Timer tim;
    bool fired = false;
    tim.set_irq_callback([&] { fired = true; });
    tim.set_prescaler(0);
    tim.set_auto_reload(5);
    tim.enable(true);
    // DIER.UIE not set
    tim.tick(5);
    EXPECT_TRUE(tim.update_flag());  // UIF still set
    EXPECT_FALSE(fired);             // but no IRQ raised
}

// ── EXTI Tests ──

TEST(ExtiTest, RegisterReadWrite) {
    Stm32f1Exti exti;
    ASSERT_TRUE(exti.write(0x00, 0x100u, Width::Word).has_value()); // IMR
    ASSERT_TRUE(exti.write(0x08, 0x200u, Width::Word).has_value()); // RTSR
    auto imr = exti.read(0x00, Width::Word);
    auto rtsr = exti.read(0x08, Width::Word);
    ASSERT_TRUE(imr.has_value() && rtsr.has_value());
    EXPECT_EQ(*imr, 0x100u);
    EXPECT_EQ(*rtsr, 0x200u);
}

TEST(ExtiTest, PendingClearsOnWrite1) {
    Stm32f1Exti exti;
    ASSERT_TRUE(exti.write(0x00, 1u, Width::Word).has_value()); // IMR line0
    ASSERT_TRUE(exti.write(0x10, 1u, Width::Word).has_value()); // SWIER line0
    EXPECT_TRUE(exti.pending(0));
    ASSERT_TRUE(exti.write(0x14, 1u, Width::Word).has_value()); // PR w1c
    EXPECT_FALSE(exti.pending(0));
}

TEST(ExtiTest, RisingEdgeOnRoutedPortTriggers) {
    Stm32f1Afio afio;
    Stm32f1Exti exti;
    exti.set_afio(afio);
    intr::intr_n_t raised = 0xFF;
    exti.set_irq_callback([&](intr::intr_n_t irq) { raised = irq; });
    ASSERT_TRUE(afio.write(0x08, 0u, Width::Word).has_value());      // EXTICR1 → PA
    ASSERT_TRUE(exti.write(0x00, 1u << 3, Width::Word).has_value()); // IMR line3
    ASSERT_TRUE(exti.write(0x08, 1u << 3, Width::Word).has_value()); // RTSR line3
    exti.on_gpio_edge({{}, 'A', 3, true});
    EXPECT_TRUE(exti.pending(3));
    EXPECT_EQ(raised, 9u); // EXTI3 → IRQ9
}

TEST(ExtiTest, EdgeOnWrongPortDoesNotTrigger) {
    Stm32f1Afio afio;
    Stm32f1Exti exti;
    exti.set_afio(afio);
    bool fired = false;
    exti.set_irq_callback([&](intr::intr_n_t) { fired = true; });
    ASSERT_TRUE(afio.write(0x08, 0u, Width::Word).has_value());      // line3 → PA
    ASSERT_TRUE(exti.write(0x00, 1u << 3, Width::Word).has_value()); // IMR
    ASSERT_TRUE(exti.write(0x08, 1u << 3, Width::Word).has_value()); // RTSR
    exti.on_gpio_edge({{}, 'B', 3, true}); // PB3 but routed to PA → no trigger
    EXPECT_FALSE(fired);
    EXPECT_FALSE(exti.pending(3));
}

TEST(ExtiTest, ImrMasksLine) {
    Stm32f1Afio afio;
    Stm32f1Exti exti;
    exti.set_afio(afio);
    bool fired = false;
    exti.set_irq_callback([&](intr::intr_n_t) { fired = true; });
    ASSERT_TRUE(afio.write(0x08, 0u, Width::Word).has_value());
    ASSERT_TRUE(exti.write(0x08, 1u << 3, Width::Word).has_value()); // RTSR line3
    // IMR line3 NOT set
    exti.on_gpio_edge({{}, 'A', 3, true});
    EXPECT_FALSE(fired);
    EXPECT_FALSE(exti.pending(3));
}

TEST(ExtiTest, FallingOnlyDoesNotTriggerOnRising) {
    Stm32f1Afio afio;
    Stm32f1Exti exti;
    exti.set_afio(afio);
    int count = 0;
    exti.set_irq_callback([&](intr::intr_n_t) { ++count; });
    ASSERT_TRUE(afio.write(0x08, 0u, Width::Word).has_value());
    ASSERT_TRUE(exti.write(0x00, 1u << 3, Width::Word).has_value()); // IMR
    ASSERT_TRUE(exti.write(0x0C, 1u << 3, Width::Word).has_value()); // FTSR only
    exti.on_gpio_edge({{}, 'A', 3, true});  // rising, only falling armed
    EXPECT_EQ(count, 0);
    exti.on_gpio_edge({{}, 'A', 3, false}); // falling → triggers
    EXPECT_EQ(count, 1);
}

// ── FLASH Tests ──

TEST(FlashTest, AcrDefault) {
    Stm32f1Flash flash;
    auto acr = flash.read(0x00, Width::Word);
    ASSERT_TRUE(acr.has_value());
    EXPECT_EQ(*acr, 0x00000030u); // PRFTBE=1, LATENCY=1
}

TEST(FlashTest, AcrWriteRead) {
    Stm32f1Flash flash;
    ASSERT_TRUE(flash.write(0x00, 0x00000002, Width::Word).has_value()); // LATENCY=2
    auto acr = flash.read(0x00, Width::Word);
    ASSERT_TRUE(acr.has_value());
    EXPECT_EQ(*acr, 0x00000002u);
}

TEST(FlashTest, KeyrWriteAccepted) {
    Stm32f1Flash flash;
    ASSERT_TRUE(flash.write(0x04, 0x45670123, Width::Word).has_value());
    auto keyr = flash.read(0x04, Width::Word);
    ASSERT_TRUE(keyr.has_value());
    EXPECT_EQ(*keyr, 0x45670123u);
}

TEST(FlashTest, SrNotBusy) {
    Stm32f1Flash flash;
    auto sr = flash.read(0x0C, Width::Word);
    ASSERT_TRUE(sr.has_value());
    EXPECT_EQ(*sr, 0u); // BSY=0
}

TEST(FlashTest, CrDefault) {
    Stm32f1Flash flash;
    auto cr = flash.read(0x10, Width::Word);
    ASSERT_TRUE(cr.has_value());
    EXPECT_TRUE(*cr & (1u << 7)); // LOCK bit set after reset
}

TEST(FlashTest, ReservedOffsetReturnsZero) {
    Stm32f1Flash flash;
    auto val = flash.read(0x50, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0u);
}

TEST(FlashTest, ReservedWriteNoFault) {
    Stm32f1Flash flash;
    EXPECT_TRUE(flash.write(0x50, 0xDEAD, Width::Word).has_value());
}

// ── AFIO Tests ──

TEST(AfioTest, MaprDefault) {
    Stm32f1Afio afio;
    auto mapr = afio.read(0x04, Width::Word);
    ASSERT_TRUE(mapr.has_value());
    EXPECT_EQ(*mapr, 0u);
}

TEST(AfioTest, MaprWriteRead) {
    Stm32f1Afio afio;
    ASSERT_TRUE(afio.write(0x04, 0x02000000, Width::Word).has_value()); // SWJ config
    auto mapr = afio.read(0x04, Width::Word);
    ASSERT_TRUE(mapr.has_value());
    EXPECT_EQ(*mapr, 0x02000000u);
}

TEST(AfioTest, ExticrWriteRead) {
    Stm32f1Afio afio;
    // EXTICR1: map PA0-PA3 as EXTI sources
    ASSERT_TRUE(afio.write(0x08, 0x00000000, Width::Word).has_value());
    auto exticr1 = afio.read(0x08, Width::Word);
    ASSERT_TRUE(exticr1.has_value());
    EXPECT_EQ(*exticr1, 0u);

    // EXTICR2: map PB4-PB7
    ASSERT_TRUE(afio.write(0x0C, 0x00001111, Width::Word).has_value());
    auto exticr2 = afio.read(0x0C, Width::Word);
    ASSERT_TRUE(exticr2.has_value());
    EXPECT_EQ(*exticr2, 0x00001111u);
}

TEST(AfioTest, ExticrAllFour) {
    Stm32f1Afio afio;
    for (int i = 0; i < 4; ++i) {
        addr_t offset = 0x08 + i * 4;
        data_t val = static_cast<data_t>(i + 1) * 0x11111111u;
        ASSERT_TRUE(afio.write(offset, val, Width::Word).has_value());
        auto r = afio.read(offset, Width::Word);
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(*r, val);
    }
}

TEST(AfioTest, EvcrWriteRead) {
    Stm32f1Afio afio;
    ASSERT_TRUE(afio.write(0x00, 0x000000C0, Width::Word).has_value());
    auto evcr = afio.read(0x00, Width::Word);
    ASSERT_TRUE(evcr.has_value());
    EXPECT_EQ(*evcr, 0x000000C0u);
}

TEST(AfioTest, Mapr2WriteRead) {
    Stm32f1Afio afio;
    ASSERT_TRUE(afio.write(0x18, 0x00000001, Width::Word).has_value());
    auto mapr2 = afio.read(0x18, Width::Word);
    ASSERT_TRUE(mapr2.has_value());
    EXPECT_EQ(*mapr2, 0x00000001u);
}

TEST(AfioTest, MmioThroughBus) {
    Bus bus;
    Stm32f1Afio afio;
    ASSERT_TRUE(bus.map(region(0x40010000, 0x400, afio.GetWeak())).has_value());

    ASSERT_TRUE(bus.write(0x4001'0004, 0x02000000, Width::Word).has_value());
    auto val = bus.read(0x4001'0004, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0x02000000u);
}

TEST(AfioTest, ReservedOffsetIsCompatibilityNoOp) {
    Stm32f1Afio afio;

    auto val = afio.read(0x50, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0u);
    EXPECT_TRUE(afio.write(0x50, 0xDEADBEEF, Width::Word).has_value());
}
