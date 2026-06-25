// E2E regression for the armcc/AC6 firmware corpus (T5c).
//
// These fixtures are PREBUILT STM32CubeCubeF1 STM32F103RB-Nucleo examples
// compiled under Keil MDK with Arm Compiler 6 (armclang), committed as ELF
// (.axf) binaries under test/firmware/armcc/. CI has no Keil, so we do not
// rebuild them; the committed .axf are the gate. They exercise armcc codegen
// (different from the gcc hal_uart sample) and validate the simulator against
// real compiler output.
//
// Regeneration recipe: see test/firmware/armcc/REGENERATE.md

#include <gtest/gtest.h>

#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "chips/stm32f1/stm32f103_soc.hpp"

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

// Boot a firmware to its steady loop; return "" on a clean run (no fault),
// otherwise a diagnostic string. Using a plain return value (not ASSERT_*)
// keeps the assertions inside the test body where gtest can short-circuit.
std::string boot_clean_or_diag(const char* path, size_t steps) {
    auto data = read_file(path);
    if (data.empty()) {
        return std::string("firmware fixture missing: ") + path;
    }
    auto soc = Stm32f103Soc::create();
    if (!soc.has_value()) {
        return "Stm32f103Soc::create() failed";
    }
    auto r = (*soc)->load_elf(data);
    if (!r.has_value()) {
        return "ELF load failed";
    }
    (*soc)->run(steps);

    auto state = (*soc)->machine().cpu->state();
    if (!state.has_value()) {
        return "cpu->state() failed";
    }
    if (*state == cpu::CPU::State::Faulted) {
        char buf[64];
        auto cm3 = (*soc)->cortex_m3_cpu();
        auto pc = cm3->pc();
        std::snprintf(buf, sizeof(buf), "faulted at PC=0x%08lx",
                      static_cast<unsigned long>(pc.has_value() ? *pc : 0xDEAD));
        return buf;
    }
    return {};
}

} // namespace

// 2,000,000 steps mirrors the budget used to reach the main loop of the Keil
// F103 firmware (see document/notes/007). The gate is "boots clean, no fault".

TEST(FirmwareArmcc, TimTimeBaseBootsClean) {
    EXPECT_EQ(boot_clean_or_diag(
                  ARMCC_FW_DIR "/nucleo_f103rb_tim_timebase.ac6.axf", 2'000'000),
              "")
        << "TIM_TimeBase (armcc/AC6) failed to boot clean";
}

TEST(FirmwareArmcc, UartPrintfBootsClean) {
    EXPECT_EQ(boot_clean_or_diag(
                  ARMCC_FW_DIR "/nucleo_f103rb_uart_printf.ac6.axf", 2'000'000),
              "")
        << "UART_Printf (armcc/AC6) failed to boot clean";
}

TEST(FirmwareArmcc, GpioIoToggleBootsClean) {
    EXPECT_EQ(boot_clean_or_diag(
                  ARMCC_FW_DIR "/nucleo_f103rb_gpio_iotoggle.ac6.axf", 2'000'000),
              "")
        << "GPIO_IOToggle (armcc/AC6) failed to boot clean";
}

// ── Multi-optimization-level corpus (E2 Tier-2, notes 018) ──
// Same three examples rebuilt at AC6 -O0/-O2/-Oz (AC6 has no -Os; -Oz is its
// min-size flag). Different -O emits different Thumb-2 mixes (e.g. -O2 may use
// adc.w/sdiv that -O0 avoids), so each variant must boot clean independently.
// Built via test/firmware/armcc/build_corpus_opt.ps1 (Windows NTFS); .text
// grows O0 > O2 > Oz, confirming the levels are distinct.

namespace {
void expect_opt_boots_clean(const char* stem, const char* opt) {
    // ARMCC_FW_DIR has no trailing '/', so add it (the original tests get it via
    // the "/nucleo_..." string-literal concatenation).
    std::string path = std::string(ARMCC_FW_DIR) + "/nucleo_f103rb_" + stem +
                       ".ac6-" + opt + ".axf";
    EXPECT_EQ(boot_clean_or_diag(path.c_str(), 2'000'000), "")
        << std::string(stem) + " -" + opt + " (armcc/AC6) failed to boot clean";
}
} // namespace

TEST(FirmwareArmcc, GpioIoToggleOptO0BootsClean) { expect_opt_boots_clean("gpio_iotoggle", "O0"); }
TEST(FirmwareArmcc, GpioIoToggleOptO2BootsClean) { expect_opt_boots_clean("gpio_iotoggle", "O2"); }
TEST(FirmwareArmcc, GpioIoToggleOptOzBootsClean) { expect_opt_boots_clean("gpio_iotoggle", "Oz"); }

TEST(FirmwareArmcc, TimTimeBaseOptO0BootsClean) { expect_opt_boots_clean("tim_timebase", "O0"); }
TEST(FirmwareArmcc, TimTimeBaseOptO2BootsClean) { expect_opt_boots_clean("tim_timebase", "O2"); }
TEST(FirmwareArmcc, TimTimeBaseOptOzBootsClean) { expect_opt_boots_clean("tim_timebase", "Oz"); }

TEST(FirmwareArmcc, UartPrintfOptO0BootsClean) { expect_opt_boots_clean("uart_printf", "O0"); }
TEST(FirmwareArmcc, UartPrintfOptO2BootsClean) { expect_opt_boots_clean("uart_printf", "O2"); }
TEST(FirmwareArmcc, UartPrintfOptOzBootsClean) { expect_opt_boots_clean("uart_printf", "Oz"); }
