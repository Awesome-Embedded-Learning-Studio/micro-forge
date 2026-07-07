// SCB (System Control Block) register tests — direct API, no bus.
// Pins every read/write branch in scb.cpp so the coverage floor matches the
// implemented behaviour (scb was at 29% with no dedicated test file).
#include "periph/scb.hpp"

#include <gtest/gtest.h>

using namespace micro_forge;
using namespace micro_forge::periph;

// ── read paths ──

TEST(ScbTest, CpuIdDefaultIsCortexM3r2p0) {
    ScbPeripheral scb;
    auto r = scb.read(0x00, Width::Word);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 0x412FC230u);
}

TEST(ScbTest, IcsrReadWriteRoundTrip) {
    ScbPeripheral scb;
    ASSERT_TRUE(scb.write(0x04, 0x12345678u, Width::Word).has_value());
    auto r = scb.read(0x04, Width::Word);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 0x12345678u);
}

TEST(ScbTest, VtorWriteInvokesCallbackAndStores) {
    ScbPeripheral scb;
    uint32_t captured = 0;
    scb.set_vtor_callback([&](uint32_t v) { captured = v; });
    ASSERT_TRUE(scb.write(0x08, 0x08000000u, Width::Word).has_value());
    EXPECT_EQ(captured, 0x08000000u);
    EXPECT_EQ(scb.read(0x08, Width::Word).value(), 0x08000000u);
}

TEST(ScbTest, ScrAndCcrReadWrite) {
    ScbPeripheral scb;
    ASSERT_TRUE(scb.write(0x10, 0xDEADBEEFu, Width::Word).has_value());
    // CCR (0x14) is read/write on ARM v7-M; writing a non-default value
    // verifies the write actually lands (was a no-op fault before the fix).
    ASSERT_TRUE(scb.write(0x14, 0x00000300u, Width::Word).has_value());
    EXPECT_EQ(scb.read(0x10, Width::Word).value(), 0xDEADBEEFu);
    EXPECT_EQ(scb.read(0x14, Width::Word).value(), 0x00000300u);
}

TEST(ScbTest, ShcsrReadWrite) {
    ScbPeripheral scb;
    ASSERT_TRUE(scb.write(0x24, 0x0000001Fu, Width::Word).has_value());
    EXPECT_EQ(scb.read(0x24, Width::Word).value(), 0x0000001Fu);
}

// ── AIRCR VECTKEY enforcement ──

TEST(ScbTest, AircrRejectsWrongVectkey) {
    ScbPeripheral scb;
    // No 0x05FA in upper 16 bits → write is ignored, AIRCR keeps its default.
    ASSERT_TRUE(scb.write(0x0C, 0x00000700u, Width::Word).has_value());
    EXPECT_EQ(scb.read(0x0C, Width::Word).value(), 0xFA050000u);
}

TEST(ScbTest, AircrAcceptsVectkeyAndFiresPrigroup) {
    ScbPeripheral scb;
    uint8_t pg = 0xFF;
    scb.set_prigroup_callback([&](uint8_t p) { pg = p; });
    // VECTKEY=0x05FA, PRIGROUP=4 (bits 10:8).
    ASSERT_TRUE(scb.write(0x0C, 0x05FA0400u, Width::Word).has_value());
    EXPECT_EQ(pg, 4u);
    auto aircr = scb.read(0x0C, Width::Word);
    ASSERT_TRUE(aircr.has_value());
    EXPECT_EQ(*aircr & 0x0000FFFFu, 0x0400u);
    EXPECT_EQ(scb.prigroup(), 4u);
}

// ── SHP registers (3 word groups + byte writes) ──

TEST(ScbTest, ShpWordWritePacksFourBytesAndReadsBack) {
    ScbPeripheral scb;
    ASSERT_TRUE(scb.write(0x18, 0x11223344u, Width::Word).has_value());
    ASSERT_TRUE(scb.write(0x1C, 0x55667788u, Width::Word).has_value());
    ASSERT_TRUE(scb.write(0x20, 0x99AABBCCu, Width::Word).has_value());
    EXPECT_EQ(scb.read(0x18, Width::Word).value(), 0x11223344u);
    EXPECT_EQ(scb.read(0x1C, Width::Word).value(), 0x55667788u);
    EXPECT_EQ(scb.read(0x20, Width::Word).value(), 0x99AABBCCu);
    // SHP word is little-endian: byte 0 = lowest exception in the group.
    // write 0x18=0x11223344 → exc4=0x44 (byte0) .. exc7=0x11 (byte3).
    EXPECT_EQ(scb.system_exception_priority(4), 0x44u);
    EXPECT_EQ(scb.system_exception_priority(7), 0x11u);
    EXPECT_EQ(scb.system_exception_priority(8), 0x88u);
    EXPECT_EQ(scb.system_exception_priority(15), 0x99u);
}

TEST(ScbTest, ShpByteWriteWithinRange) {
    ScbPeripheral scb;
    // SHP byte window 0x18-0x23: offset 0x1A → shp_[2] → exception 6.
    ASSERT_TRUE(scb.write(0x1A, 0x77u, Width::Byte).has_value());
    EXPECT_EQ(scb.system_exception_priority(6), 0x77u);
}

// ── width / decode error paths ──

TEST(ScbTest, ReadRejectsNonWordWidth) {
    ScbPeripheral scb;
    EXPECT_FALSE(scb.read(0x04, Width::Byte).has_value());
}

TEST(ScbTest, WriteRejectsByteOutsideShpRange) {
    ScbPeripheral scb;
    // Byte write but offset 0x04 is not in SHP byte window → Unaligned path.
    EXPECT_FALSE(scb.write(0x04, 0u, Width::Byte).has_value());
}

TEST(ScbTest, UnknownOffsetReadPeripheralFault) {
    ScbPeripheral scb;
    EXPECT_FALSE(scb.read(0x28, Width::Word).has_value());
}

TEST(ScbTest, UnknownOffsetWritePeripheralFault) {
    ScbPeripheral scb;
    EXPECT_FALSE(scb.write(0x28, 0u, Width::Word).has_value());
}

// ── system_exception_priority bounds ──

TEST(ScbTest, SystemExceptionPriorityBounds) {
    ScbPeripheral scb;
    // Exceptions 0..3 and 16+ have no configurable priority → 0xFF.
    EXPECT_EQ(scb.system_exception_priority(0), 0xFFu);
    EXPECT_EQ(scb.system_exception_priority(3), 0xFFu);
    EXPECT_EQ(scb.system_exception_priority(16), 0xFFu);
}
