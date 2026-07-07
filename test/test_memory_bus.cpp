#include <gtest/gtest.h>

#include "chips/stm32f1/periph/stm32f1_gpio.hpp"
#include "core/types.hpp"
#include "memory/bus.hpp"
#include "memory/flat_memory.hpp"
#include "memory/region.hpp"

using namespace micro_forge;
using namespace micro_forge::memory;

// -- Single region routing --

TEST(BusTest, SingleRegionReadWrite) {
    Bus bus;
    FlatMemory mem(256);
    ASSERT_TRUE(bus.map(region(0x1000, 256, mem.GetWeak())).has_value());

    auto wr = bus.write(0x1010, 0xCAFEBABE, Width::Word);
    ASSERT_TRUE(wr.has_value());

    auto rd = bus.read(0x1010, Width::Word);
    ASSERT_TRUE(rd.has_value());
    EXPECT_EQ(rd.value(), 0xCAFEBABEu);
}

// -- Bit-band alias (Cortex-M3 hardware feature) --

TEST(BusTest, BitBandPeripheralWriteSetsAndClearsBit) {
    Bus bus;
    FlatMemory periph(256);
    ASSERT_TRUE(bus.map(region(0x40000000, 256, periph.GetWeak())).has_value());

    // alias 0x42000000 = bit 0 of word 0x40000000.
    ASSERT_TRUE(bus.write(0x42000000, 1, Width::Word).has_value());
    auto w = bus.read(0x40000000, Width::Word);
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(*w, 1u);

    ASSERT_TRUE(bus.write(0x42000000, 0, Width::Word).has_value());
    auto w2 = bus.read(0x40000000, Width::Word);
    ASSERT_TRUE(w2.has_value());
    EXPECT_EQ(*w2, 0u);
}

TEST(BusTest, BitBandPeripheralReadReturnsBit) {
    Bus bus;
    FlatMemory periph(256);
    ASSERT_TRUE(bus.map(region(0x40000000, 256, periph.GetWeak())).has_value());

    // Set bit 16 of word 0x40000000 directly.
    ASSERT_TRUE(bus.write(0x40000000, 0x00010000u, Width::Word).has_value());
    // Alias for bit 16: byte 0x40000002 bit 0 → 0x42000000 + (2<<5) =
    // 0x42000040.
    auto v = bus.read(0x42000040, Width::Word);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 1u);
}

TEST(BusTest, BitBandSramWriteSetsBit) {
    Bus bus;
    FlatMemory sram(256);
    ASSERT_TRUE(bus.map(region(0x20000000, 256, sram.GetWeak())).has_value());

    // SRAM alias 0x22000000 = bit 0 of word 0x20000000.
    ASSERT_TRUE(bus.write(0x22000000, 1, Width::Word).has_value());
    auto w = bus.read(0x20000000, Width::Word);
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(*w, 1u);
}

// -- Dual region routing --

TEST(BusTest, DualRegionRouting) {
    Bus bus;
    FlatMemory mem_a(64);
    FlatMemory mem_b(64);

    ASSERT_TRUE(bus.map(region(0x0000, 64, mem_a.GetWeak())).has_value());
    ASSERT_TRUE(bus.map(region(0x1000, 64, mem_b.GetWeak())).has_value());

    ASSERT_TRUE(bus.write(0x0000, 0xAAAA, Width::HalfWord).has_value());
    ASSERT_TRUE(bus.write(0x1000, 0xBBBB, Width::HalfWord).has_value());

    auto a = bus.read(0x0000, Width::HalfWord);
    auto b = bus.read(0x1000, Width::HalfWord);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(a.value(), 0xAAAAu);
    EXPECT_EQ(b.value(), 0xBBBBu);

    // Verify isolation: mem_a should not see mem_b's data
    auto direct = mem_a.read(0, Width::HalfWord);
    ASSERT_TRUE(direct.has_value());
    EXPECT_EQ(direct.value(), 0xAAAAu);
}

// -- Overlap rejection --

TEST(BusTest, OverlapRejected) {
    Bus bus;
    FlatMemory mem_a(256);
    FlatMemory mem_b(256);

    ASSERT_TRUE(bus.map(region(0x0000, 256, mem_a.GetWeak())).has_value());

    auto result = bus.map(region(0x0080, 256, mem_b.GetWeak()));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BusError::RegionOverlap);
}

TEST(BusTest, AdjacentRegionsAllowed) {
    Bus bus;
    FlatMemory mem_a(128);
    FlatMemory mem_b(128);

    ASSERT_TRUE(bus.map(region(0x0000, 128, mem_a.GetWeak())).has_value());
    ASSERT_TRUE(bus.map(region(0x0080, 128, mem_b.GetWeak())).has_value());
}

// -- Unmapped address --

TEST(BusTest, UnmappedRead) {
    Bus bus;
    auto result = bus.read(0xDEAD, Width::Word);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BusError::Unmapped);
}

TEST(BusTest, UnmappedWrite) {
    Bus bus;
    auto result = bus.write(0xDEAD, 0, Width::Word);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BusError::Unmapped);
}

// -- Boundary tests --

TEST(BusTest, BoundaryFirstByte) {
    Bus bus;
    FlatMemory mem(16);
    ASSERT_TRUE(bus.map(region(0x100, 16, mem.GetWeak())).has_value());

    auto rd = bus.read(0x100, Width::Byte);
    ASSERT_TRUE(rd.has_value());
}

TEST(BusTest, BoundaryLastByte) {
    Bus bus;
    FlatMemory mem(16);
    ASSERT_TRUE(bus.map(region(0x100, 16, mem.GetWeak())).has_value());

    auto rd = bus.read(0x10F, Width::Byte);
    ASSERT_TRUE(rd.has_value());
}

TEST(BusTest, BoundaryOnePastEnd) {
    Bus bus;
    FlatMemory mem(16);
    ASSERT_TRUE(bus.map(region(0x100, 16, mem.GetWeak())).has_value());

    auto rd = bus.read(0x110, Width::Byte);
    ASSERT_FALSE(rd.has_value());
    EXPECT_EQ(rd.error(), BusError::Unmapped);
}

// -- Invalid WeakPtr rejection --

TEST(BusTest, InvalidWeakPtrRejected) {
    Bus bus;
    WeakPtr<periph::Device> null_ptr;
    auto result = bus.map(Region{0x0000, 0x0100, null_ptr});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BusError::InvalidDevice);
}

TEST(BusTest, TraceCapturesSuccessfulAndFailedAccesses) {
    Bus bus;
    FlatMemory mem(64);
    micro_forge::chips::stm32f1::Stm32f1Gpio gpio('A');
    std::vector<BusTraceEvent> events;

    ASSERT_TRUE(bus.map(region(0x0000, 64, mem.GetWeak())).has_value());
    ASSERT_TRUE(bus.map(region(0x40010800, 0x400, gpio.GetWeak())).has_value());
    bus.set_trace([&](const BusTraceEvent& event) { events.push_back(event); });

    ASSERT_TRUE(bus.write(0x0004, 0x12345678, Width::Word).has_value());
    auto rd = bus.read(0x0004, Width::Word);
    ASSERT_TRUE(rd.has_value());
    auto unmapped = bus.read(0xDEADBEEF, Width::Word);
    ASSERT_FALSE(unmapped.has_value());
    auto readonly = bus.write(0x40010808, 0x1, Width::Word);
    ASSERT_FALSE(readonly.has_value());

    ASSERT_EQ(events.size(), 4u);
    EXPECT_TRUE(events[0].is_write);
    EXPECT_TRUE(events[0].ok);
    EXPECT_EQ(events[0].value, 0x12345678u);
    EXPECT_EQ(events[0].device, "FlatMemory");

    EXPECT_FALSE(events[1].is_write);
    EXPECT_TRUE(events[1].ok);
    EXPECT_EQ(events[1].value, 0x12345678u);

    EXPECT_FALSE(events[2].is_write);
    EXPECT_FALSE(events[2].ok);
    EXPECT_EQ(events[2].error, BusError::Unmapped);
    EXPECT_EQ(events[2].device, "unmapped");

    EXPECT_TRUE(events[3].is_write);
    EXPECT_FALSE(events[3].ok);
    EXPECT_EQ(events[3].error, BusError::ReadOnly);
    EXPECT_EQ(events[3].device, "GPIOA");
}
