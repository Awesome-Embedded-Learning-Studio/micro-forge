#include "test_cortex_m3_common.hpp"

// ── LDRD / STRD (.W) — all addressing modes (matrix §5 / F32-9 gap) ──
// Encodings via arm-none-eabi objdump. Asserts concrete values + writeback,
// not round-trip: loading/storing against the same address the handler computes
// would mask a P/U/W or rt/rt2 field bug (the class of bug T1c #9/#10 had).

TEST_F(CortexM3Test, LdrdImmediateOffset) {
    // e9d1 3402 = ldrd r3, r4, [r1, #8]: [base+8]→r3, [base+12]→r4, no writeback.
    ASSERT_TRUE(mem_.write(0x108, 0xAAAA1110u, Width::Word).has_value());
    ASSERT_TRUE(mem_.write(0x10C, 0xBBBB2220u, Width::Word).has_value());
    load_program({0xE9D1, 0x3402});
    reset_cpu();
    set_reg(1, 0x100u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(3), 0xAAAA1110u);
    EXPECT_EQ(reg(4), 0xBBBB2220u);
    EXPECT_EQ(reg(1), 0x100u); // no writeback
}

TEST_F(CortexM3Test, LdrdPreIndexWriteback) {
    // e9f1 3402 = ldrd r3, r4, [r1, #8]!: addr=base+8, writeback base+8.
    ASSERT_TRUE(mem_.write(0x108, 0x1111u, Width::Word).has_value());
    ASSERT_TRUE(mem_.write(0x10C, 0x2222u, Width::Word).has_value());
    load_program({0xE9F1, 0x3402});
    reset_cpu();
    set_reg(1, 0x100u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(3), 0x1111u);
    EXPECT_EQ(reg(4), 0x2222u);
    EXPECT_EQ(reg(1), 0x108u);
}

TEST_F(CortexM3Test, LdrdPostIndexWriteback) {
    // e8f1 3402 = ldrd r3, r4, [r1], #8: addr=base, writeback base+8.
    ASSERT_TRUE(mem_.write(0x100, 0x3333u, Width::Word).has_value());
    ASSERT_TRUE(mem_.write(0x104, 0x4444u, Width::Word).has_value());
    load_program({0xE8F1, 0x3402});
    reset_cpu();
    set_reg(1, 0x100u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(3), 0x3333u);
    EXPECT_EQ(reg(4), 0x4444u);
    EXPECT_EQ(reg(1), 0x108u);
}

TEST_F(CortexM3Test, LdrdNegativePreIndex) {
    // e971 3402 = ldrd r3, r4, [r1, #-8]! (U=0): addr=base-8, writeback base-8.
    ASSERT_TRUE(mem_.write(0x0F8, 0x5555u, Width::Word).has_value());
    ASSERT_TRUE(mem_.write(0x0FC, 0x6666u, Width::Word).has_value());
    load_program({0xE971, 0x3402});
    reset_cpu();
    set_reg(1, 0x100u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(3), 0x5555u);
    EXPECT_EQ(reg(4), 0x6666u);
    EXPECT_EQ(reg(1), 0x0F8u);
}

TEST_F(CortexM3Test, StrdImmediateOffset) {
    // e9c1 3402 = strd r3, r4, [r1, #8]: r3→[base+8], r4→[base+12], no writeback.
    load_program({0xE9C1, 0x3402});
    reset_cpu();
    set_reg(1, 0x100u);
    set_reg(3, 0x7777u);
    set_reg(4, 0x8888u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    auto v1 = bus_.read(0x108, Width::Word);
    auto v2 = bus_.read(0x10C, Width::Word);
    ASSERT_TRUE(v1.has_value());
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(*v1, 0x7777u);
    EXPECT_EQ(*v2, 0x8888u);
    EXPECT_EQ(reg(1), 0x100u);
}

TEST_F(CortexM3Test, StrdPostIndexWriteback) {
    // e8e1 3402 = strd r3, r4, [r1], #8: store at base, writeback base+8.
    load_program({0xE8E1, 0x3402});
    reset_cpu();
    set_reg(1, 0x100u);
    set_reg(3, 0x9999u);
    set_reg(4, 0xAAAAu);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    auto v1 = bus_.read(0x100, Width::Word);
    auto v2 = bus_.read(0x104, Width::Word);
    ASSERT_TRUE(v1.has_value());
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(*v1, 0x9999u);
    EXPECT_EQ(*v2, 0xAAAAu);
    EXPECT_EQ(reg(1), 0x108u);
}

// ── Load/Store single (.W) imm8 modes: post-/pre-index (matrix §5 / F32-8 gap) ──
// hw2[11:8] op selects the mode (B=post+, 9=post-, F=pre+, D=pre-).

TEST_F(CortexM3Test, LdrWidePostIndexPositive) {
    // f851 3b04 = ldr.w r3, [r1], #4 (op=B): load [base], writeback base+4.
    ASSERT_TRUE(mem_.write(0x100, 0xDEADBEEFu, Width::Word).has_value());
    load_program({0xF851, 0x3B04});
    reset_cpu();
    set_reg(1, 0x100u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(3), 0xDEADBEEFu);
    EXPECT_EQ(reg(1), 0x104u);
}

TEST_F(CortexM3Test, LdrWidePostIndexNegative) {
    // f851 3904 = ldr.w r3, [r1], #-4 (op=9): load [base], writeback base-4.
    ASSERT_TRUE(mem_.write(0x100, 0xCAFEBABEu, Width::Word).has_value());
    load_program({0xF851, 0x3904});
    reset_cpu();
    set_reg(1, 0x100u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(3), 0xCAFEBABEu);
    EXPECT_EQ(reg(1), 0x0FCu);
}

TEST_F(CortexM3Test, StrWidePostIndexWriteback) {
    // f841 3b04 = str.w r3, [r1], #4 (op=B): store at base, writeback base+4.
    load_program({0xF841, 0x3B04});
    reset_cpu();
    set_reg(1, 0x100u);
    set_reg(3, 0x12345678u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    auto v = bus_.read(0x100, Width::Word);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 0x12345678u);
    EXPECT_EQ(reg(1), 0x104u);
}

TEST_F(CortexM3Test, LdrWidePreIndexWriteback) {
    // f851 3f04 = ldr.w r3, [r1, #4]! (op=F): load [base+4], writeback base+4.
    ASSERT_TRUE(mem_.write(0x104, 0x0BADF00Du, Width::Word).has_value());
    load_program({0xF851, 0x3F04});
    reset_cpu();
    set_reg(1, 0x100u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(3), 0x0BADF00Du);
    EXPECT_EQ(reg(1), 0x104u);
}

TEST_F(CortexM3Test, LdrWidePreIndexNegative) {
    // f851 3d04 = ldr.w r3, [r1, #-4]! (op=D): load [base-4], writeback base-4.
    ASSERT_TRUE(mem_.write(0x0FC, 0xFEEDFACEu, Width::Word).has_value());
    load_program({0xF851, 0x3D04});
    reset_cpu();
    set_reg(1, 0x100u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(3), 0xFEEDFACEu);
    EXPECT_EQ(reg(1), 0x0FCu);
}
