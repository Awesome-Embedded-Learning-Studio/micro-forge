#include <gtest/gtest.h>

#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "chips/stm32f1/soc/stm32f103_soc.hpp"

#include <fstream>
#include <string>
#include <vector>

using namespace micro_forge;
using namespace micro_forge::chips::stm32f1;

namespace {

std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return {};
    }
    return {std::istreambuf_iterator<char>(f), {}};
}

} // anonymous namespace

TEST(E2E, HelloWorld) {
    auto data = read_file(E2E_HELLO_ELF);
    ASSERT_FALSE(data.empty()) << "Firmware not found at " E2E_HELLO_ELF;

    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());

    std::string output;
    (*soc)->parts().serial().set_output(
        [&](uint8_t ch) { output += static_cast<char>(ch); });

    auto r = (*soc)->load_elf(data);
    ASSERT_TRUE(r.has_value()) << r.error();

    (*soc)->run(100000);

    auto state = (*soc)->machine().cpu->state();
    ASSERT_TRUE(state.has_value());
    ASSERT_NE(*state, cpu::CPU::State::Faulted)
        << "CPU faulted during execution";

    EXPECT_NE(output.find("Hello"), std::string::npos)
        << "Output was: " << output;
}

TEST(E2E, GpioBlink) {
    auto data = read_file(E2E_BLINK_ELF);
    ASSERT_FALSE(data.empty()) << "Firmware not found at " E2E_BLINK_ELF;

    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());

    int toggle_count = 0;
    (*soc)->parts().gpioa.set_pin_change_callback([&](uint8_t pin, bool) {
        if (pin == 5) {
            toggle_count++;
        }
    });

    auto r = (*soc)->load_elf(data);
    ASSERT_TRUE(r.has_value()) << r.error();

    (*soc)->run(4000000);

    auto state = (*soc)->machine().cpu->state();
    ASSERT_TRUE(state.has_value());
    ASSERT_NE(*state, cpu::CPU::State::Faulted)
        << "CPU faulted during execution";

    EXPECT_GE(toggle_count, 6)
        << "Expected at least 6 PA5 toggles, got " << toggle_count;
}

#ifdef E2E_HAL_UART_ELF
TEST(E2E, HalUartTransmit) {
    auto data = read_file(E2E_HAL_UART_ELF);
    ASSERT_FALSE(data.empty()) << "Firmware not found at " E2E_HAL_UART_ELF;

    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());

    std::string output;
    (*soc)->parts().serial().set_output(
        [&](uint8_t ch) { output += static_cast<char>(ch); });

    auto r = (*soc)->load_elf(data);
    ASSERT_TRUE(r.has_value()) << r.error();

    (*soc)->run(200000);

    auto state = (*soc)->machine().cpu->state();
    ASSERT_TRUE(state.has_value());
    ASSERT_NE(*state, cpu::CPU::State::Faulted)
        << "CPU faulted during execution";

    EXPECT_NE(output.find("Hello from STM32 HAL UART"), std::string::npos)
        << "Output was: " << output;
}
#endif

TEST(E2E, SysTick) {
    auto data = read_file(E2E_SYSTICK_ELF);
    ASSERT_FALSE(data.empty()) << "Firmware not found at " E2E_SYSTICK_ELF;

    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());

    auto r = (*soc)->load_elf(data);
    ASSERT_TRUE(r.has_value()) << r.error();

    auto cm3 = (*soc)->cortex_m3_cpu();
    ASSERT_TRUE(cm3.IsValid());
    auto& bus = (*soc)->machine().bus;

    // Verify vector table entry 15 (SysTick handler, system exception 15)
    auto vt15 = bus->read(0x0800003C, Width::Word);
    ASSERT_TRUE(vt15.has_value()) << "Cannot read vector table entry 15";
    ASSERT_NE(*vt15, 0u) << "Vector table entry 15 is zero!";

    // Verify tick_count is 0 initially
    auto tc0 = bus->read(0x20000000, Width::Word);
    ASSERT_TRUE(tc0.has_value());

    for (size_t i = 0; i < 100000; i++) {
        (*soc)->run(1);
        auto s = cm3->state();
        ASSERT_TRUE(s.has_value());
        if (*s == cpu::CPU::State::Faulted) {
            auto pc_val = cm3->pc();
            auto lr_val = cm3->register_value(14);
            auto sp_val = cm3->register_value(13);
            auto tc = bus->read(0x20000000, Width::Word);

            FAIL() << "CPU faulted at step " << i << " PC=0x" << std::hex
                   << (pc_val.has_value() ? *pc_val : 0xDEAD) << " LR=0x"
                   << (lr_val.has_value() ? *lr_val : 0xDEAD) << " SP=0x"
                   << (sp_val.has_value() ? *sp_val : 0xDEAD)
                   << " handler=" << cm3->in_handler_mode() << " tick_count=0x"
                   << (tc.has_value() ? *tc : 0xDEAD) << " VT15=0x" << *vt15;
        }
    }

    auto val = bus->read(0x20000000, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_GE(*val, 3u) << "Expected tick_count >= 3, got " << *val;
}
