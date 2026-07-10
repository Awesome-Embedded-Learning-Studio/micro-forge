#include <gtest/gtest.h>

#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "chips/stm32f1/periph/stm32f1_usart.hpp"
#include "chips/stm32f1/soc/stm32f103_soc.hpp"
#include "tools/mmio_trace.hpp"

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

    // Report the real toggle count so the blink rate is observable, and
    // tighten the floor to the real lower bound (the old >=6 was far below
    // actual and hid regressions). ~33 toggles/4M steps ≈ 300 ms/toggle at 1×.
    std::cout << "[GpioBlink] PA5 toggles over 4M steps = " << toggle_count
              << "\n";
    EXPECT_GE(toggle_count, 25)
        << "Expected >= 25 PA5 toggles, got " << toggle_count;
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

#ifdef E2E_TAMCPP_LED_ELF
// TAMCPP 1_led_control: C++ HAL LED<PC13>. Boots and toggles PC13 at least once.
TEST(E2E, TamcppLed) {
    auto data = read_file(E2E_TAMCPP_LED_ELF);
    ASSERT_FALSE(data.empty()) << "Firmware not found at " E2E_TAMCPP_LED_ELF;

    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());

    int toggle = 0;
    (*soc)->parts().gpioc.set_pin_change_callback([&](uint8_t pin, bool) {
        if (pin == 13) ++toggle;
    });

    auto r = (*soc)->load_elf(data);
    ASSERT_TRUE(r.has_value()) << r.error();

    // main loops HAL_Delay(500) between on/off; the first on() lands past 40M
    // cycles (the delay is longer than the naive 64 MHz × 500 ms estimate), so
    // run 80M to observe at least one PC13 toggle.
    (*soc)->run(80'000'000);

    auto state = (*soc)->machine().cpu->state();
    ASSERT_TRUE(state.has_value());
    ASSERT_NE(*state, cpu::CPU::State::Faulted) << "CPU faulted";
    EXPECT_GE(toggle, 1) << "PC13 should toggle at least once; got " << toggle;
}
#endif

#ifdef E2E_TAMCPP_BUTTON_ELF
// TAMCPP 2_button_control: C++ HAL Button<PA0, PullUp, Low>. Inject idle→press
// →release and verify both Pressed (led.on → PC13 BSRR reset) and Released
// (led.off → PC13 BSRR set) fire. Exercises the IT-block + ITSTATE paths.
TEST(E2E, TamcppButton) {
    using tools::enable_mmio_trace;
    using tools::MmioAccess;
    auto data = read_file(E2E_TAMCPP_BUTTON_ELF);
    ASSERT_FALSE(data.empty()) << "Firmware not found at " E2E_TAMCPP_BUTTON_ELF;

    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());

    // active-low PC13: BSRR reset half (bit 13+16) = led on, set half = led off.
    int on_w = 0, off_w = 0;
    enable_mmio_trace(*(*soc)->machine().bus, [&](const MmioAccess& a) {
        if (!a.is_write || !a.ok || a.addr != 0x4001'1010u ||
            a.width != Width::Word) {
            return;
        }
        if (a.value & (1u << (13 + 16))) ++on_w;
        if (a.value & (1u << 13)) ++off_w;
    });

    auto r = (*soc)->load_elf(data);
    ASSERT_TRUE(r.has_value()) << r.error();

    // No floating-pad model: inject the pull-up idle level before the firmware
    // reaches Button's BootSync state, then drive a clean press/release spike.
    (*soc)->parts().gpioa.simulate_input(0, true);  // idle (high)
    (*soc)->run(10'000'000);
    (*soc)->parts().gpioa.simulate_input(0, false); // press (low)
    (*soc)->run(4'000'000);                         // > 20 ms debounce
    (*soc)->parts().gpioa.simulate_input(0, true);  // release (high)
    (*soc)->run(4'000'000);                         // > 20 ms debounce

    EXPECT_GT(on_w, 0) << "Pressed should fire led.on() (BSRR reset)";
    EXPECT_GT(off_w, 0) << "Released should fire led.off() (BSRR set)";
}
#endif

#ifdef E2E_TAMCPP_UART_ELF
// TAMCPP 3_uart_logger: C++ HAL USART1. TX prints the banner; an injected RX
// line "LED ON\r\n" is parsed into "OK: LED ON\r\n". Exercises the RXNE IRQ →
// ring → handle_command path that depends on pop {pc} restoring SP correctly.
TEST(E2E, TamcppUart) {
    auto data = read_file(E2E_TAMCPP_UART_ELF);
    ASSERT_FALSE(data.empty()) << "Firmware not found at " E2E_TAMCPP_UART_ELF;

    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());

    std::string output;
    (*soc)->parts().serial().set_output(
        [&](uint8_t ch) { output += static_cast<char>(ch); });

    auto r = (*soc)->load_elf(data);
    ASSERT_TRUE(r.has_value()) << r.error();
    auto cm3 = (*soc)->cortex_m3_cpu();
    ASSERT_TRUE(cm3.IsValid());

    (*soc)->run(2'000'000); // banner + arm RXNE + enter while loop

    auto& usart1 = static_cast<Stm32f1Usart&>((*soc)->parts().serial());
    const char cmd[] = "LED ON\r\n";
    for (char c : cmd) {
        usart1.inject_rx(static_cast<uint8_t>(c));
        // Run until PC is back in main's while loop so this byte's RXNE IRQ is
        // fully entered/handled/returned before the next byte. PC range is the
        // uart.elf main while body — re-check if the firmware is rebuilt.
        for (int i = 0; i < 200'000; ++i) {
            (*soc)->run(1'000);
            const uint32_t pc = cm3->pc().value_or(0);
            if (pc >= 0x0800'02B2u && pc <= 0x0800'0334u) break;
        }
    }
    (*soc)->run(5'000'000);

    EXPECT_NE(output.find("UART Logger Ready!"), std::string::npos)
        << "TX banner missing; output: " << output;
    EXPECT_NE(output.find("OK: LED ON"), std::string::npos)
        << "RX command response missing; output: " << output;
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
