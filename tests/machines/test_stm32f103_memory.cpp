#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>

#include "micro_forge/basic_engine.hpp"
#include "micro_forge/kernel/bus/bus.hpp"
#include "micro_forge/machines/stm32f103.hpp"
#include "micro_forge/device/base/hooks/event_bus.hpp"
#include "micro_forge/device/stm32f1/stm32f1_gpio.hpp"

namespace {
using Machine = micro_forge::machines::Stm32F103Machine;
using Engine = micro_forge::BasicEngine<Machine>;
using Error = micro_forge::kernel::BusError;
static_assert(micro_forge::MachineModel<Machine>);
static_assert(micro_forge::ConsoleMachine<Machine>);
static_assert(micro_forge::SerialInputMachine<Machine>);
static_assert(!micro_forge::SnapshotMachine<Machine>);
static_assert(!std::is_copy_constructible_v<Machine>);
static_assert(!std::is_move_constructible_v<Machine>);

TEST(Stm32MemoryTest, FlashAndBootAliasShareStorageButRejectGuestWrites) {
    Engine engine(std::in_place);
    auto& bus = engine.machine().bus();
    const std::array<uint8_t, 4> bytes = {0x78, 0x56, 0x34, 0x12};
    ASSERT_TRUE(engine.load_image(Machine::kFlashBase + 12, bytes));
    EXPECT_EQ(bus.read_u32(12), 0x12345678U);
    EXPECT_EQ(bus.read_u32(Machine::kFlashBase + 12), 0x12345678U);
    const auto alias_write = bus.write_u32(12, 0);
    ASSERT_FALSE(alias_write);
    EXPECT_EQ(alias_write.error().kind, Error::Kind::kReadOnly);
    EXPECT_EQ(alias_write.error().address, 12U);
    const auto physical_write = bus.write_u8(Machine::kFlashBase + 12, 0);
    ASSERT_FALSE(physical_write);
    EXPECT_EQ(physical_write.error().kind, Error::Kind::kReadOnly);
    EXPECT_EQ(physical_write.error().address, Machine::kFlashBase + 12);
    EXPECT_EQ(bus.read_u32(12), 0x12345678U);
    const std::array<uint8_t, 1> replacement = {0xAA};
    ASSERT_TRUE(engine.load_image(12, replacement));
    EXPECT_EQ(bus.read_u8(Machine::kFlashBase + 12), 0xAAU);
}

TEST(Stm32MemoryTest, FirmwarePlacementRejectsMmioOverflowAndCrossWindowWrites) {
    Machine machine;
    const std::array<uint8_t, 4> bytes = {1, 2, 3, 4};
    EXPECT_FALSE(machine.load_image(0x4001'3804, bytes));
    EXPECT_FALSE(machine.load_image(std::numeric_limits<uint64_t>::max(), bytes));
    EXPECT_FALSE(machine.load_image(Machine::kFlashBase + Machine::kFlashSize - 2, bytes));
    EXPECT_FALSE(machine.load_image(Machine::kSramBase + Machine::kSramSize - 2, bytes));
    EXPECT_EQ(machine.bus().read_u8(Machine::kFlashBase + Machine::kFlashSize - 1), 0xFFU);
    EXPECT_EQ(machine.bus().read_u8(Machine::kSramBase + Machine::kSramSize - 1), 0U);
    ASSERT_TRUE(machine.load_image(Machine::kSramBase, bytes));
    EXPECT_EQ(machine.bus().read_u32(Machine::kSramBase), 0x04030201U);
}

TEST(Stm32MemoryTest, BitBandModifiesOnlyOneBitAndPreservesAliasFaultAddress) {
    Machine machine;
    auto& bus = machine.bus();
    constexpr uint32_t alias = 0x2200'0000 + 3 * 32 + 5 * 4;
    ASSERT_TRUE(bus.write_u32(Machine::kSramBase, 0x01020304));
    ASSERT_TRUE(bus.write_u32(alias, 1));
    EXPECT_EQ(bus.read_u32(Machine::kSramBase), 0x21020304U);
    EXPECT_EQ(bus.read_u32(alias), 1U);
    ASSERT_TRUE(bus.write_u32(alias, 2)); // Bit-band writes consume bit zero, not truthiness.
    EXPECT_EQ(bus.read_u32(Machine::kSramBase), 0x01020304U);
    ASSERT_TRUE(bus.write_u32(alias, 0xFFFF'FFFF));
    EXPECT_EQ(bus.read_u32(alias), 1U);
    ASSERT_TRUE(bus.write_u32(alias, 0xFFFF'FFFE));
    EXPECT_EQ(bus.read_u32(Machine::kSramBase), 0x01020304U);
    const auto narrow = bus.write_u16(alias, 1);
    ASSERT_FALSE(narrow);
    EXPECT_EQ(narrow.error().kind, Error::Kind::kUnaligned);
    EXPECT_EQ(narrow.error().address, alias);
    constexpr uint32_t outside = 0x2200'0000 + Machine::kSramSize * 32;
    const auto error = bus.read_u32(outside);
    ASSERT_FALSE(error);
    EXPECT_EQ(error.error().address, outside);
}

TEST(Stm32MemoryTest, PeripheralBitBandUsesTheRealWordRegisterAndEmitsAnEdge) {
    Machine machine;
    auto& bus = machine.bus();
    unsigned edges = 0;
    machine.events().gpio.connect([&](const auto& event) {
        EXPECT_EQ(event.port, 'A');
        EXPECT_EQ(event.pin, 13);
        EXPECT_TRUE(event.rising);
        ++edges;
    });
    constexpr uint32_t gpio_odr = 0x4001'080C;
    constexpr uint32_t alias = 0x4200'0000 + (gpio_odr - 0x4000'0000) * 32 + 13 * 4;
    ASSERT_TRUE(bus.write_u32(alias, 1));
    EXPECT_EQ(bus.read_u32(gpio_odr), 1U << 13);
    ASSERT_NE(machine.gpio('A'), nullptr);
    EXPECT_TRUE(machine.gpio('A')->get_pin(13));
    EXPECT_EQ(machine.gpio('D'), nullptr);
    EXPECT_EQ(edges, 1U);
}

TEST(Stm32MemoryTest, RegisterBridgeRejectsWidthsAndUnknownOffsetsRatherThanByteSplitting) {
    Machine machine;
    auto& bus = machine.bus();
    const auto byte = bus.write_u8(0x4002'1000, 1);
    ASSERT_FALSE(byte);
    EXPECT_EQ(byte.error().kind, Error::Kind::kUnaligned);
    const auto wide = bus.write_u64(0x4002'1000, 1);
    ASSERT_FALSE(wide);
    EXPECT_EQ(wide.error().kind, Error::Kind::kUnaligned);
    const auto unknown = bus.read_u32(0x4002'1010);
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().kind, Error::Kind::kDeviceFault);
    EXPECT_EQ(unknown.error().address, 0x4002'1010U);
    EXPECT_EQ(bus.read_u32(0x4002'1000), 0x83U);
}

TEST(Stm32MemoryTest, BothUsartsHaveIndependentReceiveRegistersAndTaggedOutput) {
    Engine engine(std::in_place);
    auto& machine = engine.machine();
    auto& bus = machine.bus();
    std::array<unsigned, 3> units{};
    machine.events().uart.connect([&](const auto& event) { ++units.at(event.unit); });
    ASSERT_TRUE(bus.write_u32(0x4001'3804, 'A'));
    ASSERT_TRUE(bus.write_u32(0x4000'4404, 'B'));
    EXPECT_EQ(std::string_view(engine.console().data(), engine.console().size()), "AB");
    EXPECT_EQ(units[1], 1U);
    EXPECT_EQ(units[2], 1U);
    engine.inject_serial('x');
    machine.inject_serial2('y');
    EXPECT_EQ(bus.read_u32(0x4001'3804), static_cast<uint32_t>('x'));
    EXPECT_EQ(bus.read_u32(0x4000'4404), static_cast<uint32_t>('y'));
}

} // namespace
