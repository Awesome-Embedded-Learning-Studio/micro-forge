#include <gtest/gtest.h>

#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "arch/arm/cortex_m3/cortex_m3_reset.hpp"
#include "chips/stm32f1/soc/stm32f103_soc.hpp"

using namespace micro_forge;
using namespace micro_forge::chips::stm32f1;

TEST(SocTest, CreateSuccess) {
    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());

    auto& parts = (*soc)->parts();
    EXPECT_EQ(parts.rcc.name(), "RCC");
    EXPECT_EQ(parts.gpioa.name(), "GPIOA");
    EXPECT_EQ(parts.usart1.name(), "USART");
    EXPECT_EQ(parts.tim2.name(), "TIM");
}

TEST(SocTest, MemoryAccessible) {
    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());

    auto& bus = *(*soc)->machine().bus;

    // Write to Flash
    ASSERT_TRUE(bus.write(0x0800'0000, 0xDEADBEEF, Width::Word).has_value());
    auto val = bus.read(0x0800'0000, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0xDEADBEEFu);

    // Write to SRAM
    ASSERT_TRUE(bus.write(0x2000'0000, 0x12345678, Width::Word).has_value());
    val = bus.read(0x2000'0000, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0x12345678u);
}

TEST(SocTest, PeripheralsAccessible) {
    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());

    auto& bus = *(*soc)->machine().bus;

    // Read RCC CR reset value
    auto cr = bus.read(0x4002'1000, Width::Word);
    ASSERT_TRUE(cr.has_value());
    EXPECT_EQ(*cr, 0x00000083u);
}

TEST(SocTest, CannotMove) {
    static_assert(!std::is_move_constructible_v<Stm32f103Soc>);
    static_assert(!std::is_copy_constructible_v<Stm32f103Soc>);
}

TEST(SocTest, InterfaceAccessors) {
    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());

    auto& parts = (*soc)->parts();

    // Gpio interface accessor
    auto& gpio_a = parts.gpio('A');
    gpio_a.set_pin(5, true);
    EXPECT_TRUE(gpio_a.get_pin(5));

    // SerialPort interface accessor
    auto& serial = parts.serial();
    EXPECT_TRUE(serial.can_send());

    // Timer interface accessor
    auto& timer = parts.timer();
    timer.set_auto_reload(100);
    timer.enable(true);
    EXPECT_EQ(timer.counter(), 0u);

    // ClockController interface accessor
    auto& clocks = parts.clocks();
    EXPECT_FALSE(clocks.is_clock_enabled(0x40010800));
}

// ── Reset sequence test ──

TEST(ResetTest, VectorTableLoad) {
    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());

    auto& bus = *(*soc)->machine().bus;

    // Write vector table at 0x00000000
    ASSERT_TRUE(
        bus.write(0x00000000, 0x20005000, Width::Word).has_value()); // SP
    ASSERT_TRUE(bus.write(0x00000004, 0x08000101, Width::Word)
                    .has_value()); // PC (Thumb)

    auto cm3 = (*soc)->cortex_m3_cpu();
    ASSERT_TRUE(cm3.IsValid());
    auto r = cpu::arm::cortex_m3::cortex_m3_reset(*cm3, bus, 0x00000000);
    ASSERT_TRUE(r.has_value());

    auto sp = cm3->register_value(13);
    ASSERT_TRUE(sp.has_value());
    EXPECT_EQ(*sp, 0x20005000u);

    auto pc = cm3->pc();
    ASSERT_TRUE(pc.has_value());
    // PC stores execution address; Thumb bit stripped, tracked by XPSR.T
    EXPECT_EQ(*pc, 0x08000100u);
}

TEST(ResetTest, ThumbBitForced) {
    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());

    auto& bus = *(*soc)->machine().bus;

    // Write vector table with PC bit[0] = 0
    ASSERT_TRUE(bus.write(0x00000000, 0x20005000, Width::Word).has_value());
    ASSERT_TRUE(bus.write(0x00000004, 0x08000100, Width::Word)
                    .has_value()); // No Thumb bit

    auto cm3 = (*soc)->cortex_m3_cpu();
    ASSERT_TRUE(cm3.IsValid());
    auto r = cpu::arm::cortex_m3::cortex_m3_reset(*cm3, bus, 0x00000000);
    ASSERT_TRUE(r.has_value());

    auto pc = cm3->pc();
    ASSERT_TRUE(pc.has_value());
    // Thumb bit is stripped from PC; stored in XPSR.T instead
    EXPECT_EQ(*pc, 0x08000100u);
}
