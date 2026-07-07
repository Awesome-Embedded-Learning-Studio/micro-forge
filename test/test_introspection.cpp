// Tests for the structured introspection snapshot (cli::read_introspection).
// It is the single source of truth shared by the CLI JSON serializer and the
// GUI dashboard (milestone 04). These tests pin: (1) the 7 newly-exposed
// status/mask/stack registers are readable; (2) read_introspection is a pure
// read — it must not mutate simulator state; (3) on real firmware it observes
// the expected advance (state/cycles/pc).
#include "cli/introspection.hpp"
#include "chips/stm32f1/stm32f103_soc.hpp"
#include "cpu/cpu.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace micro_forge;
using micro_forge::cli::read_introspection;
using micro_forge::chips::stm32f1::Stm32f103Soc;
using micro_forge::cpu::CPU;

namespace {
std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
} // namespace

TEST(Introspection, FreshSocHasHaltedCpuAndNoFault) {
    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());
    const auto snap = read_introspection(**soc, "");

    EXPECT_EQ(snap.cpu.state, CPU::State::Halted);
    EXPECT_FALSE(snap.cpu.handler_mode);
    EXPECT_FALSE(snap.fault.present);
    EXPECT_EQ(snap.cycles, 0u);
    for (std::size_t r = 0; r < snap.cpu.regs.size(); ++r) {
        EXPECT_EQ(snap.cpu.regs[r], 0u) << "r" << r;
    }
    // The 7 newly-exposed status/mask/stack registers are readable post-create.
    EXPECT_EQ(snap.cpu.xpsr, 0u);
    EXPECT_EQ(snap.cpu.primask, 0u);
    EXPECT_EQ(snap.cpu.basepri, 0u);
    EXPECT_EQ(snap.cpu.faultmask, 0u);
    EXPECT_EQ(snap.cpu.control, 0u);
    EXPECT_EQ(snap.cpu.msp, 0u);
    EXPECT_EQ(snap.cpu.psp, 0u);
}

TEST(Introspection, IsPureReadAndDoesNotMutateState) {
    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());
    const auto before = read_introspection(**soc, "");
    const auto after = read_introspection(**soc, "");

    EXPECT_EQ(after.cpu.state, before.cpu.state);
    EXPECT_EQ(after.cpu.pc, before.cpu.pc);
    EXPECT_EQ(after.cycles, before.cycles);
    EXPECT_EQ(after.fault.present, before.fault.present);
}

TEST(Introspection, UsartOutputIsForwardedIntoPeripherals) {
    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());
    const std::string_view msg = "hello gui";
    const auto snap = read_introspection(**soc, msg);
    EXPECT_EQ(snap.peripherals.usart_output, msg);
}

TEST(Introspection, FreshSocPeripheralsAreQuiescent) {
    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());
    const auto snap = read_introspection(**soc, "");

    EXPECT_EQ(snap.peripherals.gpio[0].port, 'A');
    EXPECT_EQ(snap.peripherals.gpio[1].port, 'B');
    EXPECT_EQ(snap.peripherals.gpio[2].port, 'C');
    for (const auto& g : snap.peripherals.gpio) {
        EXPECT_EQ(g.odr, 0u);
    }
    EXPECT_EQ(snap.peripherals.systick.ctrl, 0u);
    EXPECT_EQ(snap.peripherals.systick.load, 0u);
    EXPECT_FALSE(snap.peripherals.nvic.has_pending);
    EXPECT_EQ(snap.peripherals.nvic.highest_pending_irq, 0xFF);
    EXPECT_EQ(snap.peripherals.nvic.enabled_count, 0u);
}

#ifdef INTROSPECTION_HELLO_ELF
TEST(Introspection, RunningFirmwareAdvancesCyclesAndKeepsPcInFlash) {
    const auto data = read_file(INTROSPECTION_HELLO_ELF);
    ASSERT_FALSE(data.empty());

    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());
    ASSERT_TRUE((*soc)->load_elf(data).has_value());

    static_cast<void>((*soc)->run(50000));
    const auto snap = read_introspection(**soc, "");

    EXPECT_EQ(snap.cpu.state, CPU::State::Running);
    EXPECT_GT(snap.cycles, 0u);
    // STM32F103 flash is 0x08000000..0x0807FFFF (512 KiB page-0 bank).
    EXPECT_GE(snap.cpu.pc, 0x08000000u);
    EXPECT_LT(snap.cpu.pc, 0x08080000u);
}
#endif
