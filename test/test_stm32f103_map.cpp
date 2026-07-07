#include <gtest/gtest.h>

#include "chips/stm32f1/soc/memory_bus.hpp"
#include "core/types.hpp"
#include "memory/bus.hpp"
#include "memory/flat_memory.hpp"

using namespace micro_forge;
using namespace micro_forge::memory;
using namespace micro_forge::chips::stm32f1;

class STM32F103Test : public ::testing::Test {
  protected:
    Bus bus;
    FlatMemory flash{128 * 1024};
    FlatMemory sram{20 * 1024};

    void SetUp() override {
        auto result = configure_memory(bus, flash, sram);
        ASSERT_TRUE(result.has_value());
    }
};

// -- Flash area --

TEST_F(STM32F103Test, FlashReadWrite) {
    auto wr = bus.write(0x0800'0000, 0x12345678, Width::Word);
    ASSERT_TRUE(wr.has_value());

    auto rd = bus.read(0x0800'0000, Width::Word);
    ASSERT_TRUE(rd.has_value());
    EXPECT_EQ(rd.value(), 0x12345678u);
}

// -- SRAM area --

TEST_F(STM32F103Test, SramReadWrite) {
    auto wr = bus.write(0x2000'0000, 0xABCD, Width::HalfWord);
    ASSERT_TRUE(wr.has_value());

    auto rd = bus.read(0x2000'0000, Width::HalfWord);
    ASSERT_TRUE(rd.has_value());
    EXPECT_EQ(rd.value(), 0xABCDu);
}

// -- Boot alias --

TEST_F(STM32F103Test, BootAliasReflectsFlash) {
    ASSERT_TRUE(bus.write(0x0800'0100, 0xDEAD, Width::HalfWord).has_value());

    auto via_flash = bus.read(0x0800'0100, Width::HalfWord);
    auto via_boot = bus.read(0x0000'0100, Width::HalfWord);

    ASSERT_TRUE(via_flash.has_value());
    ASSERT_TRUE(via_boot.has_value());
    EXPECT_EQ(via_flash.value(), via_boot.value());
}

// -- Unmapped areas --

TEST_F(STM32F103Test, UnmappedPeripheralArea) {
    auto rd = bus.read(0x4000'0000, Width::Word);
    ASSERT_FALSE(rd.has_value());
    EXPECT_EQ(rd.error(), BusError::Unmapped);
}

TEST_F(STM32F103Test, UnmappedNVICArea) {
    auto rd = bus.read(0xE000'0000, Width::Word);
    ASSERT_FALSE(rd.has_value());
    EXPECT_EQ(rd.error(), BusError::Unmapped);
}
