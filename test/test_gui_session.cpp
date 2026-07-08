// Unit tests for the GUI model layer (micro_forge::gui::model::Session).
//
// Session is deliberately Qt-free, so these run under plain gtest with no
// QApplication — they pin: (1) rebuild yields a valid Halted session with no
// firmware; (2) loading + running real firmware advances cycles, keeps PC in
// flash, and drains USART output into the buffer; (3) rebuild clears the
// USART buffer so a reset doesn't show stale output.
#include "gui/model/session.hpp"

#include "cpu/cpu.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

using micro_forge::cpu::CPU;
using micro_forge::gui::model::Session;

TEST(GuiSession, RebuildWithoutFirmwareIsValidAndHalted) {
    Session s;
    ASSERT_TRUE(s.rebuild().has_value());
    EXPECT_TRUE(s.valid());
    const auto snap = s.snapshot();
    EXPECT_EQ(snap.cpu.state, CPU::State::Halted);
    EXPECT_EQ(snap.cycles, 0u);
    EXPECT_TRUE(s.usart_output().empty());
}

TEST(GuiSession, StepOnHaltedSessionStaysValid) {
    Session s;
    ASSERT_TRUE(s.rebuild().has_value());
    s.step(); // no firmware → Halted; step is a no-op but must not corrupt state
    EXPECT_TRUE(s.valid());
    EXPECT_EQ(s.snapshot().cpu.state, CPU::State::Halted);
}

#ifdef SESSION_HELLO_ELF
TEST(GuiSession, LoadsFirmwareAndRunsAndEmitsUsart) {
    Session s;
    s.set_firmware(SESSION_HELLO_ELF);
    ASSERT_TRUE(s.rebuild().has_value())
        << "rebuild failed for " << SESSION_HELLO_ELF;
    s.run(50000);

    const auto snap = s.snapshot();
    EXPECT_EQ(snap.cpu.state, CPU::State::Running);
    EXPECT_GT(snap.cycles, 0u);
    // STM32F103 flash window.
    EXPECT_GE(snap.cpu.pc, 0x08000000u);
    EXPECT_LT(snap.cpu.pc, 0x08080000u);
    EXPECT_FALSE(s.usart_output().empty())
        << "hello firmware produced no USART output";
}

TEST(GuiSession, RebuildClearsUsartBuffer) {
    Session s;
    s.set_firmware(SESSION_HELLO_ELF);
    ASSERT_TRUE(s.rebuild().has_value());
    s.run(50000);
    ASSERT_FALSE(s.usart_output().empty());

    // A second rebuild (e.g. Reset button) must drop the accumulated output.
    ASSERT_TRUE(s.rebuild().has_value());
    EXPECT_TRUE(s.usart_output().empty())
        << "rebuild should clear the USART buffer";
}
#endif
