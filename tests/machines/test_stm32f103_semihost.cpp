#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "micro_forge/arch/arm/cortex_m3/cortex_m3.hpp"
#include "micro_forge/basic_engine.hpp"
#include "micro_forge/kernel/bus/bus.hpp"
#include "micro_forge/machines/stm32f103.hpp"
#include "micro_forge/device/base/hooks/event_bus.hpp"

namespace {
using Machine = micro_forge::machines::Stm32F103Machine;
using micro_forge::StopReason;

class Stm32SemihostTest : public testing::Test {
protected:
    micro_forge::BasicEngine<Machine> engine{std::in_place};
    std::vector<uint8_t> image = std::vector<uint8_t>(0x200, 0);

    void word(uint32_t offset, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) {
            image.at(offset + i) = static_cast<uint8_t>(value >> (8U * i));
        }
    }
    void half(uint32_t offset, uint16_t value) {
        image.at(offset) = static_cast<uint8_t>(value);
        image.at(offset + 1) = static_cast<uint8_t>(value >> 8U);
    }
    void program(uint8_t operation, uint32_t argument) {
        word(0, Machine::kSramBase + Machine::kSramSize);
        word(4, Machine::kFlashBase + 0x101);
        word(3 * 4, Machine::kFlashBase + 0x181); // A real HardFault handler.
        half(0x100, static_cast<uint16_t>(0x2000U | operation));
        half(0x102, 0x4902); // LDR r1,[pc,#8] -> argument literal
        half(0x104, 0xBEAB);
        half(0x106, 0xE7FE);
        word(0x10C, argument);
        half(0x180, 0xE7FE);
        image[0x120] = 'H';
        image[0x121] = 'i';
        image[0x122] = 0;
    }
    void boot() {
        ASSERT_TRUE(engine.load_image(Machine::kFlashBase, image));
        ASSERT_TRUE(engine.machine().reset());
    }
    std::string_view output() {
        const auto bytes = engine.console();
        return {bytes.data(), bytes.size()};
    }
};

TEST_F(Stm32SemihostTest, IsOptInAndDoesNotPretendDebuggerOutputCameFromUsart) {
    program(3, Machine::kFlashBase + 0x120);
    boot();
    EXPECT_EQ(engine.run(10).reason, StopReason::kBudgetExhausted);
    EXPECT_TRUE(engine.machine().cpu().in_handler_mode());
    EXPECT_TRUE(engine.console().empty());
    engine.machine().set_semihosting_enabled(true);
    unsigned uart_events = 0;
    engine.machine().events().uart.connect([&](const auto&) { ++uart_events; });
    boot();
    EXPECT_EQ(engine.run(10).reason, StopReason::kBudgetExhausted);
    EXPECT_FALSE(engine.machine().cpu().in_handler_mode());
    EXPECT_EQ(output(), "H");
    EXPECT_EQ(uart_events, 0U);
    boot(); // Reset retains the debugger policy but clears console output.
    EXPECT_TRUE(engine.console().empty());
    EXPECT_EQ(engine.run(10).reason, StopReason::kBudgetExhausted);
    EXPECT_EQ(output(), "H");
}

TEST_F(Stm32SemihostTest, WriteZeroUsesTheSharedGuestMemoryAndARequiredTerminator) {
    engine.machine().set_semihosting_enabled(true);
    program(4, Machine::kFlashBase + 0x120);
    boot();
    EXPECT_EQ(engine.run(10).reason, StopReason::kBudgetExhausted);
    EXPECT_EQ(output(), "Hi");
    const std::vector<uint8_t> unterminated(4096, 'X');
    ASSERT_TRUE(engine.load_image(Machine::kFlashBase + 0x1000, unterminated));
    program(4, Machine::kFlashBase + 0x1000);
    boot();
    const auto result = engine.run(10);
    EXPECT_EQ(result.reason, StopReason::kFault);
    EXPECT_EQ(result.detail, Machine::kFlashBase + 0x2000);
    EXPECT_TRUE(engine.console().empty()); // Rejected calls don't emit partial output.
}

TEST_F(Stm32SemihostTest, InvalidDebuggerPointerCannotReadPeripheralRegistersOrHostMemory) {
    engine.machine().set_semihosting_enabled(true);
    program(3, 0x4001'3800);
    boot();
    const auto result = engine.run(10);
    EXPECT_EQ(result.reason, StopReason::kFault);
    EXPECT_EQ(result.pc, Machine::kFlashBase + 0x104);
    EXPECT_EQ(result.detail, 0x4001'3800U);
    EXPECT_TRUE(engine.console().empty());
    EXPECT_EQ(engine.run(0).reason, StopReason::kFault);
}

TEST_F(Stm32SemihostTest, WriteIsBoundedAndOnlySupportsConsoleHandles) {
    engine.machine().set_semihosting_enabled(true);
    program(5, Machine::kSramBase);
    boot();
    auto& bus = engine.machine().bus();
    ASSERT_TRUE(bus.write_u32(Machine::kSramBase, 1));
    ASSERT_TRUE(bus.write_u32(Machine::kSramBase + 4, Machine::kFlashBase + 0x120));
    ASSERT_TRUE(bus.write_u32(Machine::kSramBase + 8, 2));
    EXPECT_EQ(engine.run(10).reason, StopReason::kBudgetExhausted);
    EXPECT_EQ(output(), "Hi");
    EXPECT_EQ(engine.machine().cpu().register_value(0).value(), 0U);
    boot();
    ASSERT_TRUE(bus.write_u32(Machine::kSramBase, 123));
    EXPECT_EQ(engine.run(10).reason, StopReason::kBudgetExhausted);
    EXPECT_TRUE(engine.console().empty());
    EXPECT_EQ(engine.machine().cpu().register_value(0).value(), 2U);
    boot();
    ASSERT_TRUE(bus.write_u32(Machine::kSramBase, 1));
    ASSERT_TRUE(bus.write_u32(Machine::kSramBase + 8, 65537));
    EXPECT_EQ(engine.run(10).reason, StopReason::kBudgetExhausted);
    EXPECT_TRUE(engine.console().empty());
    EXPECT_EQ(engine.machine().cpu().register_value(0).value(), 65537U);
}

TEST_F(Stm32SemihostTest, ExtendedExitPreservesGuestCodeAndLatchesStopWithoutExtraExecution) {
    engine.machine().set_semihosting_enabled(true);
    program(0x20, Machine::kSramBase);
    boot();
    ASSERT_TRUE(engine.machine().bus().write_u32(Machine::kSramBase, 0x20026));
    ASSERT_TRUE(engine.machine().bus().write_u32(Machine::kSramBase + 4, 42));
    const auto result = engine.run(100);
    EXPECT_EQ(result.reason, StopReason::kExited);
    EXPECT_EQ(result.code, 42U);
    EXPECT_EQ(result.detail, 0x20026U);
    EXPECT_EQ(engine.machine().cpu().cycles().value(), 3U);
    EXPECT_EQ(engine.run(100).reason, StopReason::kExited);
    EXPECT_EQ(engine.machine().cpu().cycles().value(), 3U);
}

} // namespace
