#include <gtest/gtest.h>

#include <micro_forge/kernel/bus/bus.hpp>
#include <micro_forge/kernel/memory/ram_region.hpp>
#include <micro_forge/kernel/memory/register_region.hpp>

#include <array>
#include <cstdint>

namespace {

using micro_forge::kernel::Bus;
using micro_forge::kernel::BusError;
using micro_forge::kernel::RamRegion;

TEST(RamRegionTest, WideAccessComposesLittleEndian) {
    RamRegion ram(8);
    ASSERT_TRUE(ram.write_u32(0, 0x12345678U).has_value());

    const auto b0 = ram.read_u8(0);
    ASSERT_TRUE(b0.has_value());
    EXPECT_EQ(0x78, *b0);
    const auto b1 = ram.read_u8(1);
    ASSERT_TRUE(b1.has_value());
    EXPECT_EQ(0x56, *b1);
    const auto b2 = ram.read_u8(2);
    ASSERT_TRUE(b2.has_value());
    EXPECT_EQ(0x34, *b2);
    const auto b3 = ram.read_u8(3);
    ASSERT_TRUE(b3.has_value());
    EXPECT_EQ(0x12, *b3);

    const auto half_lo = ram.read_u16(0);
    ASSERT_TRUE(half_lo.has_value());
    EXPECT_EQ(0x5678, *half_lo);
    const auto half_hi = ram.read_u16(2);
    ASSERT_TRUE(half_hi.has_value());
    EXPECT_EQ(0x1234, *half_hi);

    const auto word = ram.read_u32(0);
    ASSERT_TRUE(word.has_value());
    EXPECT_EQ(0x12345678U, *word);
}

TEST(RamRegionTest, OutOfBoundsAccessFails) {
    RamRegion ram(8);

    const auto read = ram.read_u8(8);
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(BusError::Kind::kOutOfBounds, read.error().kind);
    EXPECT_EQ(8, read.error().address);

    const auto write = ram.write_u32(6, 0xAABBCCDDU);
    ASSERT_FALSE(write.has_value());
    EXPECT_EQ(BusError::Kind::kOutOfBounds, write.error().kind);
    // The first out-of-bounds byte is at offset 8 (third byte of the word).
    EXPECT_EQ(8, write.error().address);
}

TEST(BusTest, RoutesToMappedRegions) {
    Bus bus;
    RamRegion ram_a(0x100);
    RamRegion ram_b(0x100);
    ASSERT_TRUE(bus.map(0x00000000, 0x100, ram_a).has_value());
    ASSERT_TRUE(bus.map(0x20000000, 0x100, ram_b).has_value());

    ASSERT_TRUE(bus.write_u32(0x00000000, 0x11111111U).has_value());
    ASSERT_TRUE(bus.write_u32(0x20000000, 0x22222222U).has_value());

    const auto a = bus.read_u32(0x00000000);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(0x11111111U, *a);
    const auto b = bus.read_u32(0x20000000);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(0x22222222U, *b);

    // The u16 store must hit the upper half of ram_b's word without
    // disturbing the lower half.
    ASSERT_TRUE(bus.write_u16(0x20000002, 0x3333).has_value());
    const auto b_hi = bus.read_u16(0x20000002);
    ASSERT_TRUE(b_hi.has_value());
    EXPECT_EQ(0x3333, *b_hi);
    const auto b_after = bus.read_u32(0x20000000);
    ASSERT_TRUE(b_after.has_value());
    EXPECT_EQ(0x33332222U, *b_after);
}

TEST(BusTest, UnmappedAccessFails) {
    Bus bus;
    RamRegion ram(0x100);
    ASSERT_TRUE(bus.map(0x00000000, 0x100, ram).has_value());

    const auto read = bus.read_u32(0xDEADBEECU);
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(BusError::Kind::kUnmappedRead, read.error().kind);
    EXPECT_EQ(0xDEADBEECU, read.error().address);

    const auto write = bus.write_u8(0x00000100, 0x00);
    ASSERT_FALSE(write.has_value());
    EXPECT_EQ(BusError::Kind::kUnmappedWrite, write.error().kind);
    EXPECT_EQ(0x00000100, write.error().address);
}

TEST(BusTest, OverlapRejected) {
    Bus bus;
    RamRegion ram_a(0x1000);
    RamRegion ram_b(0x1000);
    RamRegion ram_c(0x1000);
    ASSERT_TRUE(bus.map(0x1000, 0x1000, ram_a).has_value());

    // New range straddles the tail of the existing mapping.
    const auto straddle = bus.map(0x1800, 0x1000, ram_b);
    ASSERT_FALSE(straddle.has_value());
    EXPECT_EQ(BusError::Kind::kOverlap, straddle.error().kind);
    EXPECT_EQ(0x1800, straddle.error().address);

    // Adjacent mappings are legal.
    EXPECT_TRUE(bus.map(0x2000, 0x1000, ram_b).has_value());

    // New range straddles the head of an existing mapping.
    const auto head = bus.map(0x0000, 0x1001, ram_c);
    ASSERT_FALSE(head.has_value());
    EXPECT_EQ(BusError::Kind::kOverlap, head.error().kind);
    EXPECT_EQ(0x1000, head.error().address);
}

TEST(BusTest, InvalidMappingRejected) {
    Bus bus;
    RamRegion ram(0x100);

    const auto zero = bus.map(0x1000, 0, ram);
    ASSERT_FALSE(zero.has_value());
    EXPECT_EQ(BusError::Kind::kInvalidMapping, zero.error().kind);

    // base + size wraps around the top of the address space.
    const auto wrap = bus.map(UINT64_MAX - 1, 2, ram);
    ASSERT_FALSE(wrap.has_value());
    EXPECT_EQ(BusError::Kind::kInvalidMapping, wrap.error().kind);
}

TEST(BusTest, RegionErrorAddressIsAbsolute) {
    Bus bus;
    RamRegion ram(0x100);
    ASSERT_TRUE(bus.map(0x20000000, 0x100, ram).has_value());

    // The low byte lands on the last mapped byte; the high byte is out of
    // bounds, and the bus must report the absolute guest address.
    const auto read = bus.read_u16(0x200000FF);
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(BusError::Kind::kOutOfBounds, read.error().kind);
    EXPECT_EQ(0x20000100, read.error().address);
}

TEST(BusTest, DirectRamChecksBothMappingAndBackingBounds) {
    Bus bus;
    RamRegion ram(0x100);
    ASSERT_TRUE(bus.map_ram(0x1000, 0x80, ram).has_value());
    uintptr_t host = 0;
    ASSERT_TRUE(bus.direct_ram(0x1078, 8, host));
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ram.data() + 0x78), host);
    EXPECT_FALSE(bus.direct_ram(0x1079, 8, host));
    EXPECT_FALSE(bus.direct_ram(0x1080, 1, host));
    EXPECT_FALSE(bus.direct_ram(0xFFF, 8, host));
    EXPECT_FALSE(bus.direct_ram(UINT64_MAX - 1, 8, host));

    // An oversized bus mapping does not extend the region's storage.
    ASSERT_TRUE(bus.map_ram(0x2000, 0x200, ram).has_value());
    EXPECT_TRUE(bus.direct_ram(0x20F8, 8, host));
    EXPECT_FALSE(bus.direct_ram(0x20F9, 8, host));
}

TEST(BusTest, DirectRamSupportsLinearAddressZeroAndExcludesMmio) {
    Bus bus;
    RamRegion ram(RamRegion::WindowBase{0}, 0x100);
    ASSERT_TRUE(bus.map_ram(0, 0x100, ram).has_value());
    uintptr_t host = 123;
    ASSERT_TRUE(bus.direct_ram(0, 8, host));
    EXPECT_EQ(0U, host); // valid wasm offset; never dereference it in a native test

    std::array<uint8_t, 8> registers{};
    micro_forge::kernel::RegisterRegionBase mmio(registers.data(), registers.size());
    ASSERT_TRUE(bus.map(0x1000, registers.size(), mmio).has_value());
    EXPECT_FALSE(bus.direct_ram(0x1000, 8, host));
    ASSERT_TRUE(bus.write_u64(0x1000, 42).has_value());
    EXPECT_EQ(42U, bus.read_u64(0x1000).value());
    EXPECT_TRUE(bus.direct_ram(0, 8, host)); // MMIO does not evict the RAM window
}

class WatchedRam : public RamRegion {
public:
    using RamRegion::RamRegion;
    unsigned reads = 0;

    std::expected<uint64_t, BusError> read_u64(uint64_t offset) override {
        ++reads;
        return RamRegion::read_u64(offset);
    }
};

TEST(BusTest, OrdinaryMappingPreservesRamSubclassCallbacks) {
    Bus bus;
    WatchedRam ram(0x100);
    ASSERT_TRUE(bus.map(0, ram.size(), ram).has_value());
    uintptr_t host = 0;
    EXPECT_FALSE(bus.direct_ram(0, 8, host)); // only map_ram() opts into bypassing hooks
    ASSERT_TRUE(bus.read_u64(0).has_value());
    EXPECT_EQ(1U, ram.reads);
}

} // namespace
