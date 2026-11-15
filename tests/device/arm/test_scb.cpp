// SCB (System Control Block) register tests — direct API, no bus.
// Pins every read/write branch in scb.cpp so the coverage floor matches the
// implemented behaviour (scb was at 29% with no dedicated test file).
#include "micro_forge/device/arm/scb.hpp"
#include "micro_forge/device/base/types.hpp"

#include <cstdint>
#include <gtest/gtest.h>

using namespace micro_forge::device;
using namespace micro_forge::device::arm;
using namespace micro_forge::device;

// ── read paths ──

TEST(ScbTest, CpuIdDefaultIsCortexM3r2p0) {
    ScbPeripheral scb;
    auto r = scb.read(0x00U, Width::kWord);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 0x412FC230U);
}

TEST(ScbTest, IcsrReadWriteRoundTrip) {
    ScbPeripheral scb;
    ASSERT_TRUE(scb.write(0x04U, 0x12345678U, Width::kWord).has_value());
    auto r = scb.read(0x04U, Width::kWord);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 0x12345678U);
}

TEST(ScbTest, VtorWriteInvokesCallbackAndStores) {
    ScbPeripheral scb;
    uint32_t captured = 0U;
    scb.set_vtor_callback([&](uint32_t v) { captured = v; });
    ASSERT_TRUE(scb.write(0x08U, 0x08000000U, Width::kWord).has_value());
    EXPECT_EQ(captured, 0x08000000U);
    EXPECT_EQ(scb.read(0x08U, Width::kWord).value(), 0x08000000U);
}

TEST(ScbTest, ScrAndCcrReadWrite) {
    ScbPeripheral scb;
    ASSERT_TRUE(scb.write(0x10U, 0xDEADBEEFU, Width::kWord).has_value());
    // CCR (0x14) is read/write on ARM v7-M; writing a non-default value
    // verifies the write actually lands (was a no-op fault before the fix).
    ASSERT_TRUE(scb.write(0x14U, 0x00000300U, Width::kWord).has_value());
    EXPECT_EQ(scb.read(0x10U, Width::kWord).value(), 0xDEADBEEFU);
    EXPECT_EQ(scb.read(0x14U, Width::kWord).value(), 0x00000300U);
}

TEST(ScbTest, ShcsrReadWrite) {
    ScbPeripheral scb;
    ASSERT_TRUE(scb.write(0x24U, 0x0000001FU, Width::kWord).has_value());
    EXPECT_EQ(scb.read(0x24U, Width::kWord).value(), 0x0000001FU);
}

// ── AIRCR VECTKEY enforcement ──

TEST(ScbTest, AircrRejectsWrongVectkey) {
    ScbPeripheral scb;
    // No 0x05FA in upper 16 bits → write is ignored, AIRCR keeps its default.
    ASSERT_TRUE(scb.write(0x0CU, 0x00000700U, Width::kWord).has_value());
    EXPECT_EQ(scb.read(0x0CU, Width::kWord).value(), 0xFA050000U);
}

TEST(ScbTest, AircrAcceptsVectkeyAndFiresPrigroup) {
    ScbPeripheral scb;
    uint8_t pg = 0xFFU;
    scb.set_prigroup_callback([&](uint8_t p) { pg = p; });
    // VECTKEY=0x05FA, PRIGROUP=4 (bits 10:8).
    ASSERT_TRUE(scb.write(0x0CU, 0x05FA0400U, Width::kWord).has_value());
    EXPECT_EQ(pg, 4U);
    auto aircr = scb.read(0x0CU, Width::kWord);
    ASSERT_TRUE(aircr.has_value());
    EXPECT_EQ(*aircr & 0x0000FFFFU, 0x0400U);
    EXPECT_EQ(scb.prigroup(), 4U);
}

// ── SHP registers (3 word groups + byte writes) ──

TEST(ScbTest, ShpWordWritePacksFourBytesAndReadsBack) {
    ScbPeripheral scb;
    ASSERT_TRUE(scb.write(0x18U, 0x11223344U, Width::kWord).has_value());
    ASSERT_TRUE(scb.write(0x1CU, 0x55667788U, Width::kWord).has_value());
    ASSERT_TRUE(scb.write(0x20U, 0x99AABBCCU, Width::kWord).has_value());
    EXPECT_EQ(scb.read(0x18U, Width::kWord).value(), 0x11223344U);
    EXPECT_EQ(scb.read(0x1CU, Width::kWord).value(), 0x55667788U);
    EXPECT_EQ(scb.read(0x20U, Width::kWord).value(), 0x99AABBCCU);
    // SHP word is little-endian: byte 0 = lowest exception in the group.
    // write 0x18=0x11223344 → exc4=0x44 (byte0) .. exc7=0x11 (byte3).
    EXPECT_EQ(scb.system_exception_priority(4U), 0x44U);
    EXPECT_EQ(scb.system_exception_priority(7U), 0x11U);
    EXPECT_EQ(scb.system_exception_priority(8U), 0x88U);
    EXPECT_EQ(scb.system_exception_priority(15U), 0x99U);
}

TEST(ScbTest, ShpByteWriteWithinRange) {
    ScbPeripheral scb;
    // SHP byte window 0x18-0x23: offset 0x1A → shp_[2] → exception 6.
    ASSERT_TRUE(scb.write(0x1AU, 0x77U, Width::kByte).has_value());
    EXPECT_EQ(scb.system_exception_priority(6U), 0x77U);
}

// ── width / decode error paths ──

TEST(ScbTest, ReadRejectsNonWordWidth) {
    ScbPeripheral scb;
    EXPECT_FALSE(scb.read(0x04U, Width::kByte).has_value());
}

TEST(ScbTest, WriteRejectsByteOutsideShpRange) {
    ScbPeripheral scb;
    // Byte write but offset 0x04 is not in SHP byte window → Unaligned path.
    EXPECT_FALSE(scb.write(0x04U, 0U, Width::kByte).has_value());
}

TEST(ScbTest, UnknownOffsetReadPeripheralFault) {
    ScbPeripheral scb;
    EXPECT_FALSE(scb.read(0x28U, Width::kWord).has_value());
}

TEST(ScbTest, UnknownOffsetWritePeripheralFault) {
    ScbPeripheral scb;
    EXPECT_FALSE(scb.write(0x28U, 0U, Width::kWord).has_value());
}

// ── system_exception_priority bounds ──

TEST(ScbTest, SystemExceptionPriorityBounds) {
    ScbPeripheral scb;
    // Exceptions 0..3 and 16+ have no configurable priority → 0xFF.
    EXPECT_EQ(scb.system_exception_priority(0U), 0xFFU);
    EXPECT_EQ(scb.system_exception_priority(3U), 0xFFU);
    EXPECT_EQ(scb.system_exception_priority(16U), 0xFFU);
}
