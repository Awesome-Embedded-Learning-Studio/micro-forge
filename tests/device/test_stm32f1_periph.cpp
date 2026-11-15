#include <cstdint>
#include <gtest/gtest.h>

#include "micro_forge/device/base/gpio.hpp"
#include "micro_forge/device/base/types.hpp"
#include "micro_forge/device/stm32f1/stm32f1_gpio.hpp"
#include "micro_forge/device/stm32f1/stm32f1_rcc.hpp"
#include "micro_forge/device/stm32f1/stm32f1_timer.hpp"
#include "micro_forge/device/stm32f1/stm32f1_usart.hpp"

using namespace micro_forge::device;
using namespace micro_forge::device::stm32f1;

// ── RCC Tests ──

TEST(RccTest, ReadResetValues) {
    Stm32f1Rcc rcc;
    auto cr = rcc.read(0x00U, Width::kWord);
    ASSERT_TRUE(cr.has_value());
    EXPECT_EQ(*cr, 0x00000083U);

    auto apb2 = rcc.read(0x18U, Width::kWord);
    ASSERT_TRUE(apb2.has_value());
    EXPECT_EQ(*apb2, 0U);
}

TEST(RccTest, WriteApb2Enable) {
    Stm32f1Rcc rcc;
    ASSERT_TRUE(rcc.write(0x18U, 0x00000004U, Width::kWord).has_value());
    auto val = rcc.read(0x18U, Width::kWord);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0x00000004U);
}

TEST(RccTest, RegisterAccess) {
    Stm32f1Rcc rcc;

    // Write APB2ENR via device registers
    ASSERT_TRUE(rcc.write(0x18U, 0x00000004U, Width::kWord).has_value());
    auto val = rcc.read(0x18U, Width::kWord);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0x00000004U);
}

TEST(RccTest, InvalidOffset) {
    Stm32f1Rcc rcc;
    auto r = rcc.read(0x20U, Width::kWord);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), BusError::kPeripheralFault);
}

TEST(RccTest, ClockEnabledCheck) {
    Stm32f1Rcc rcc;
    EXPECT_FALSE(rcc.is_clock_enabled(0x40010800U)); // GPIOA not enabled
    rcc.enable_clock(0x40010800U);
    EXPECT_TRUE(rcc.is_clock_enabled(0x40010800U));
}

TEST(RccTest, HsiDefaultReady) {
    Stm32f1Rcc rcc;
    auto cr = rcc.read(0x00U, Width::kWord);
    ASSERT_TRUE(cr.has_value());
    EXPECT_TRUE(*cr & (1U << 0U)); // HSION
    EXPECT_TRUE(*cr & (1U << 1U)); // HSIRDY
}

TEST(RccTest, HseOnSetsReady) {
    Stm32f1Rcc rcc;
    ASSERT_TRUE(rcc.write(0x00U, 0x00010000U, Width::kWord).has_value()); // set HSEON
    auto cr = rcc.read(0x00U, Width::kWord);
    ASSERT_TRUE(cr.has_value());
    EXPECT_TRUE(*cr & (1U << 16U)); // HSEON
    EXPECT_TRUE(*cr & (1U << 17U)); // HSERDY
}

TEST(RccTest, PllOnSetsReady) {
    Stm32f1Rcc rcc;
    ASSERT_TRUE(rcc.write(0x00U, 0x01000000U, Width::kWord).has_value()); // set PLLON
    auto cr = rcc.read(0x00U, Width::kWord);
    ASSERT_TRUE(cr.has_value());
    EXPECT_TRUE(*cr & (1U << 24U)); // PLLON
    EXPECT_TRUE(*cr & (1U << 25U)); // PLLRDY
}

TEST(RccTest, DisableClearsReady) {
    Stm32f1Rcc rcc;
    // Turn on HSE
    ASSERT_TRUE(rcc.write(0x00U, 0x00010000U, Width::kWord).has_value());
    // Turn off HSE (clear HSEON)
    ASSERT_TRUE(rcc.write(0x00U, 0x00000000U, Width::kWord).has_value());
    auto cr = rcc.read(0x00U, Width::kWord);
    ASSERT_TRUE(cr.has_value());
    EXPECT_FALSE(*cr & (1U << 16U)); // HSEON off
    EXPECT_FALSE(*cr & (1U << 17U)); // HSERDY off
}

TEST(RccTest, CfgrSwSyncsToSws) {
    Stm32f1Rcc rcc;
    // SW = 0b10 (HSE), expect SWS = 0b10
    ASSERT_TRUE(rcc.write(0x04U, 0x00000002U, Width::kWord).has_value());
    auto cfgr = rcc.read(0x04U, Width::kWord);
    ASSERT_TRUE(cfgr.has_value());
    EXPECT_EQ((*cfgr >> 0U) & 0x3U, 0x2U); // SW = HSE
    EXPECT_EQ((*cfgr >> 2U) & 0x3U, 0x2U); // SWS = HSE
}

TEST(RccTest, CfgrSwPllSyncsToSws) {
    Stm32f1Rcc rcc;
    // SW = 0b11 (PLL)
    ASSERT_TRUE(rcc.write(0x04U, 0x00000003U, Width::kWord).has_value());
    auto cfgr = rcc.read(0x04U, Width::kWord);
    ASSERT_TRUE(cfgr.has_value());
    EXPECT_EQ((*cfgr >> 2U) & 0x3U, 0x3U); // SWS follows SW
}

// ── GPIO Tests ──

TEST(GpioTest, WriteOdrAndRead) {
    Stm32f1Gpio gpio('A');
    ASSERT_TRUE(gpio.write(0x0CU, 0x0020U, Width::kWord).has_value());
    auto val = gpio.read(0x0CU, Width::kWord);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0x0020U);
}

TEST(GpioTest, BsrrSetAndReset) {
    Stm32f1Gpio gpio('A');

    // Set pin 5
    ASSERT_TRUE(gpio.write(0x10U, 0x00000020U, Width::kWord).has_value());
    auto odr = gpio.read(0x0CU, Width::kWord);
    ASSERT_TRUE(odr.has_value());
    EXPECT_TRUE((*odr >> 5U) & 1U);

    // reset pin 5 via high 16 bits
    ASSERT_TRUE(gpio.write(0x10U, 0x00200000U, Width::kWord).has_value());
    odr = gpio.read(0x0CU, Width::kWord);
    ASSERT_TRUE(odr.has_value());
    EXPECT_FALSE((*odr >> 5U) & 1U);
}

TEST(GpioTest, BsrrReadOnly) {
    Stm32f1Gpio gpio('A');
    auto r = gpio.read(0x10U, Width::kWord);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), BusError::kPeripheralFault);
}

TEST(GpioTest, IdrReadOnly) {
    Stm32f1Gpio gpio('A');
    auto r = gpio.write(0x08U, 0xFFU, Width::kWord);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), BusError::kReadOnly);
}

TEST(GpioTest, SimulateInput) {
    Stm32f1Gpio gpio('A');
    gpio.simulate_input(3U, true);
    auto idr = gpio.read(0x08U, Width::kWord);
    ASSERT_TRUE(idr.has_value());
    EXPECT_TRUE((*idr >> 3U) & 1U);
}

TEST(GpioTest, PinChangeCallback) {
    Stm32f1Gpio gpio('A');
    uint8_t changed_pin = 0xFFU;
    bool changed_high = false;
    gpio.set_pin_change_callback([&](uint8_t pin, bool high) {
        changed_pin = pin;
        changed_high = high;
    });

    ASSERT_TRUE(gpio.write(0x0CU, 0x0020U, Width::kWord).has_value()); // pin 5 high
    EXPECT_EQ(changed_pin, 5U);
    EXPECT_TRUE(changed_high);
}

TEST(GpioTest, SetPinInterface) {
    Stm32f1Gpio gpio('A');
    gpio.set_pin(3U, true);
    EXPECT_TRUE(gpio.get_pin(3U));
    gpio.set_pin(3U, false);
    EXPECT_FALSE(gpio.get_pin(3U));
}

TEST(GpioTest, ConfigurePin) {
    Stm32f1Gpio gpio('A');
    gpio.configure_pin(0U, PinMode::kOutput, PinPull::kNone, PinSpeed::kHigh);
    auto crl = gpio.read(0x00U, Width::kWord);
    ASSERT_TRUE(crl.has_value());
    // Pin 0 should have non-zero config (output 50MHz push-pull = 0x3)
    EXPECT_EQ(*crl & 0xFU, 0x3U);
}

TEST(GpioTest, NameHandlesValidAndInvalidPorts) {
    EXPECT_EQ(Stm32f1Gpio('A').name(), "GPIOA");
    EXPECT_EQ(Stm32f1Gpio('E').name(), "GPIOE");
    EXPECT_EQ(Stm32f1Gpio('@').name(), "GPIO?");
    EXPECT_EQ(Stm32f1Gpio('F').name(), "GPIO?");
}

TEST(GpioTest, RegisterAccess) {
    Stm32f1Gpio gpio('A');

    ASSERT_TRUE(gpio.write(0x10U, 0x00000020U, Width::kWord).has_value()); // BSRR set pin5
    auto odr = gpio.read(0xcU, Width::kWord);
    ASSERT_TRUE(odr.has_value());
    EXPECT_TRUE((*odr >> 5U) & 1U);
}

// ── USART Tests ──

TEST(UsartTest, WriteDrOutputsChar) {
    Stm32f1Usart usart;
    uint8_t captured = 0U;
    usart.set_output([&](uint8_t ch) { captured = ch; });

    ASSERT_TRUE(usart.write(0x04U, 'H', Width::kWord).has_value());
    EXPECT_EQ(captured, 'H');
}

TEST(UsartTest, SrTxeAlwaysHigh) {
    Stm32f1Usart usart;
    auto sr = usart.read(0x00U, Width::kWord);
    ASSERT_TRUE(sr.has_value());
    EXPECT_TRUE(*sr & 0x80U);
}

TEST(UsartTest, SrTcAlwaysHigh) {
    Stm32f1Usart usart;
    auto sr = usart.read(0x00U, Width::kWord);
    ASSERT_TRUE(sr.has_value());
    EXPECT_TRUE(*sr & 0x40U);
}

TEST(UsartTest, WriteCr1) {
    Stm32f1Usart usart;
    ASSERT_TRUE(usart.write(0x0CU, 0x000CU, Width::kWord).has_value());
    auto cr1 = usart.read(0x0CU, Width::kWord);
    ASSERT_TRUE(cr1.has_value());
    EXPECT_EQ(*cr1, 0x000CU);
}

TEST(UsartTest, CanSend) {
    Stm32f1Usart usart;
    EXPECT_TRUE(usart.can_send());
}

TEST(UsartTest, SendByteInterface) {
    Stm32f1Usart usart;
    uint8_t captured = 0U;
    usart.set_output([&](uint8_t ch) { captured = ch; });
    usart.send_byte('X');
    EXPECT_EQ(captured, 'X');
}

TEST(UsartTest, RegisterAccess) {
    Stm32f1Usart usart;

    uint8_t captured = 0U;
    usart.set_output([&](uint8_t ch) { captured = ch; });

    ASSERT_TRUE(usart.write(0x4U, 'A', Width::kWord).has_value());
    EXPECT_EQ(captured, 'A');
}

TEST(UsartTest, InjectRxSetsRxne) {
    Stm32f1Usart usart;
    usart.inject_rx('X');
    auto sr = usart.read(0x00U, Width::kWord);
    ASSERT_TRUE(sr.has_value());
    EXPECT_TRUE(*sr & (1U << 5U)); // RXNE
}

TEST(UsartTest, DrReadReturnsByteAndClearsRxne) {
    Stm32f1Usart usart;
    usart.inject_rx('Q');
    auto dr = usart.read(0x04U, Width::kWord);
    ASSERT_TRUE(dr.has_value());
    EXPECT_EQ(*dr, static_cast<data_t>('Q'));
    auto sr = usart.read(0x00U, Width::kWord);
    ASSERT_TRUE(sr.has_value());
    EXPECT_FALSE(*sr & (1U << 5U)); // RXNE cleared
}

TEST(UsartTest, RxneIrqRaisedWhenEnabled) {
    Stm32f1Usart usart;
    ASSERT_TRUE(usart.write(0x0CU, 1U << 5U, Width::kWord).has_value()); // CR1 RXNEIE
    bool fired = false;
    usart.set_irq_callback([&] { fired = true; });
    usart.inject_rx('A');
    EXPECT_TRUE(fired);
}

TEST(UsartTest, RxneIrqNotRaisedWhenDisabled) {
    Stm32f1Usart usart;
    bool fired = false;
    usart.set_irq_callback([&] { fired = true; });
    usart.inject_rx('A'); // RXNEIE not set
    EXPECT_FALSE(fired);
}

// ── Timer Tests ──

TEST(TimerTest, TickIncrements) {
    Stm32f1Timer tim;
    tim.set_auto_reload(100U);
    tim.enable(true);
    tim.tick(10U);
    EXPECT_EQ(tim.counter(), 10U);
}

TEST(TimerTest, Prescaler) {
    Stm32f1Timer tim;
    tim.set_auto_reload(100U);
    tim.set_prescaler(9U);
    tim.enable(true);
    tim.tick(100U);
    EXPECT_EQ(tim.counter(), 10U);
}

TEST(TimerTest, PrescalerResidualAccumulatesAcrossSmallTicks) {
    Stm32f1Timer tim;
    tim.set_auto_reload(100U);
    tim.set_prescaler(9U);
    tim.enable(true);

    for (int i = 0U; i < 100U; ++i) {
        tim.tick(1U);
    }

    EXPECT_EQ(tim.counter(), 10U);
}

TEST(TimerTest, OverflowSetsUif) {
    Stm32f1Timer tim;
    tim.set_auto_reload(10U);
    tim.enable(true);
    tim.tick(10U);
    EXPECT_FALSE(tim.update_flag());
    EXPECT_EQ(tim.counter(), 10U);
    tim.tick(1U);
    EXPECT_TRUE(tim.update_flag());
    EXPECT_EQ(tim.counter(), 0U);
}

TEST(TimerTest, DisabledNoTick) {
    Stm32f1Timer tim;
    tim.set_auto_reload(100U);
    tim.enable(false);
    tim.tick(100U);
    EXPECT_EQ(tim.counter(), 0U);
}

TEST(TimerTest, TimerInterface) {
    Stm32f1Timer tim;
    tim.set_auto_reload(100U);
    tim.set_prescaler(0U);
    tim.enable(true);
    tim.tick(50U);
    EXPECT_EQ(tim.counter(), 50U);
    EXPECT_FALSE(tim.update_flag());

    tim.tick(60U);
    EXPECT_TRUE(tim.update_flag());
    tim.clear_update_flag();
    EXPECT_FALSE(tim.update_flag());
}

TEST(TimerTest, RegisterAccess) {
    Stm32f1Timer tim;

    ASSERT_TRUE(tim.write(0x0U, 0x0001U, Width::kWord).has_value()); // CR1=CEN
    ASSERT_TRUE(tim.write(0x2cU, 100U, Width::kWord).has_value());   // ARR
    ASSERT_TRUE(tim.write(0x28U, 0U, Width::kWord).has_value());     // PSC

    tim.tick(10U);
    auto cnt = tim.read(0x24U, Width::kWord);
    ASSERT_TRUE(cnt.has_value());
    EXPECT_EQ(*cnt, 10U);
}

TEST(TimerTest, UifEdgeWithUieTriggersIrqCallback) {
    // UIF 0→1 edge + DIER.UIE → irq_callback fires exactly once; re-overflow
    // while UIF stays set does not re-fire (edge); clearing UIF re-arms.
    Stm32f1Timer tim;
    int count = 0U;
    tim.set_irq_callback([&] { ++count; });
    tim.set_prescaler(0U); // divisor 1
    tim.set_auto_reload(5U);
    ASSERT_TRUE(tim.write(0x0CU, 1U, Width::kWord).has_value()); // DIER.UIE
    tim.enable(true);                                            // CR1.CEN

    tim.tick(6U); // ARR=5 gives a six-cycle period and a UIF edge
    EXPECT_TRUE(tim.update_flag());
    EXPECT_EQ(count, 1U);

    tim.tick(12U); // UIF still set → no re-fire (edge)
    EXPECT_EQ(count, 1U);

    tim.clear_update_flag(); // re-arm
    tim.tick(6U);
    EXPECT_EQ(count, 2U);
}

TEST(TimerTest, UifWithoutUieDoesNotFireCallback) {
    Stm32f1Timer tim;
    bool fired = false;
    tim.set_irq_callback([&] { fired = true; });
    tim.set_prescaler(0U);
    tim.set_auto_reload(5U);
    tim.enable(true);
    // DIER.UIE not set
    tim.tick(6U);
    EXPECT_TRUE(tim.update_flag()); // UIF still set
    EXPECT_FALSE(fired);            // but no IRQ raised
}

TEST(TimerTest, CounterUsesCorrectOffsetAndSixteenBitRegisters) {
    Stm32f1Timer tim;
    ASSERT_TRUE(tim.write(0x24U, 0x12345678U, Width::kWord).has_value());
    ASSERT_TRUE(tim.write(0x28U, 0x12345678U, Width::kWord).has_value());
    ASSERT_TRUE(tim.write(0x2CU, 0x12345678U, Width::kWord).has_value());
    EXPECT_EQ(tim.read(0x24U, Width::kWord).value(), 0x5678U);
    EXPECT_EQ(tim.read(0x28U, Width::kWord).value(), 0x5678U);
    EXPECT_EQ(tim.read(0x2CU, Width::kWord).value(), 0x5678U);
    EXPECT_EQ(tim.read(0x34U, Width::kWord).error(), BusError::kPeripheralFault);
    EXPECT_EQ(tim.write(0x34U, 1U, Width::kWord).error(), BusError::kPeripheralFault);
    tim.set_prescaler(0x10001U);
    tim.set_auto_reload(0x10002U);
    EXPECT_EQ(tim.read(0x28U, Width::kWord).value(), 1U);
    EXPECT_EQ(tim.read(0x2CU, Width::kWord).value(), 2U);
}

TEST(TimerTest, UpdateGenerationClearsCounterAndPrescalerPhase) {
    Stm32f1Timer tim;
    unsigned irqs = 0U;
    tim.set_irq_callback([&] { ++irqs; });
    tim.set_prescaler(9U);
    tim.set_auto_reload(100U);
    tim.enable(true);
    ASSERT_TRUE(tim.write(0x0CU, 1U, Width::kWord).has_value());
    tim.tick(29U);
    EXPECT_EQ(tim.counter(), 2U);
    ASSERT_TRUE(tim.write(0x14U, 1U, Width::kWord).has_value());
    EXPECT_EQ(tim.counter(), 0U);
    EXPECT_TRUE(tim.update_flag());
    EXPECT_EQ(irqs, 1U);
    EXPECT_EQ(tim.read(0x14U, Width::kWord).value(), 0U);
    tim.tick(1U);
    EXPECT_EQ(tim.counter(), 0U);
    tim.tick(9U);
    EXPECT_EQ(tim.counter(), 1U);
}

TEST(TimerTest, Cr2AndSmcrStoreWordConfigurationIndependently) {
    Stm32f1Timer tim;
    EXPECT_EQ(tim.read(0x04U, Width::kWord).value(), 0U);
    EXPECT_EQ(tim.read(0x08U, Width::kWord).value(), 0U);
    ASSERT_TRUE(tim.write(0x04U, 0x70U, Width::kWord).has_value());
    ASSERT_TRUE(tim.write(0x08U, 0x77U, Width::kWord).has_value());
    EXPECT_EQ(tim.read(0x04U, Width::kWord).value(), 0x70U);
    EXPECT_EQ(tim.read(0x08U, Width::kWord).value(), 0x77U);
    EXPECT_EQ(tim.read(0x00U, Width::kWord).value(), 0U);
    for (auto offset : {0x04U, 0x08U}) {
        EXPECT_EQ(tim.read(offset, Width::kByte).error(), BusError::kUnaligned);
        EXPECT_EQ(tim.write(offset, 0U, Width::kHalfWord).error(), BusError::kUnaligned);
    }
    EXPECT_EQ(tim.read(0x04U, Width::kWord).value(), 0x70U);
    EXPECT_EQ(tim.read(0x08U, Width::kWord).value(), 0x77U);
    EXPECT_EQ(tim.read(0x18U, Width::kWord).error(), BusError::kPeripheralFault);
    EXPECT_EQ(tim.write(0x18U, 0U, Width::kWord).error(), BusError::kPeripheralFault);
}

TEST(TimerTest, HalStyleBaseStartReadsSmcrBeforeEnablingCounter) {
    Stm32f1Timer tim;
    unsigned irqs = 0U;
    tim.set_irq_callback([&] { ++irqs; });
    ASSERT_TRUE(tim.write(0x28U, 1U, Width::kWord).has_value()); // PSC
    ASSERT_TRUE(tim.write(0x2CU, 2U, Width::kWord).has_value()); // ARR
    ASSERT_TRUE(tim.write(0x14U, 1U, Width::kWord).has_value()); // EGR.UG
    ASSERT_TRUE(tim.write(0x10U, 0U, Width::kWord).has_value()); // Clear UIF
    ASSERT_TRUE(tim.write(0x0CU, 1U, Width::kWord).has_value()); // DIER.UIE
    const auto slave_mode = tim.read(0x08U, Width::kWord);
    ASSERT_TRUE(slave_mode.has_value());
    EXPECT_EQ(*slave_mode & 7U, 0U);                             // Internal-clock base timer.
    ASSERT_TRUE(tim.write(0x00U, 1U, Width::kWord).has_value()); // CR1.CEN
    tim.tick(5U);
    EXPECT_EQ(tim.counter(), 2U);
    EXPECT_EQ(irqs, 0U);
    tim.tick(1U);
    EXPECT_EQ(tim.counter(), 0U);
    EXPECT_TRUE(tim.update_flag());
    EXPECT_EQ(irqs, 1U);
}

TEST(TimerTest, FullSixteenBitReloadPeriodIs65536Cycles) {
    Stm32f1Timer tim;
    tim.enable(true);
    tim.tick(65535U);
    EXPECT_EQ(tim.counter(), 65535U);
    EXPECT_FALSE(tim.update_flag());
    tim.tick(1U);
    EXPECT_EQ(tim.counter(), 0U);
    EXPECT_TRUE(tim.update_flag());
}
