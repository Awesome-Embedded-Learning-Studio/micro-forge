#include "test_cortex_m3_common.hpp"

TEST_F(CortexM3Test, ExtendAndReverseInstructions) {
    load_program({
        0xB248, // sxtb r0, r1
        0xB21A, // sxth r2, r3
        0xB2EC, // uxtb r4, r5
        0xB2BE, // uxth r6, r7
        0xBA08, // rev r0, r1
        0xBA5A, // rev16 r2, r3
        0xBAEC, // revsh r4, r5
    });
    reset_cpu();
    set_reg(1, 0x12345680);
    set_reg(3, 0x00008001);
    set_reg(5, 0x0000AB80);
    set_reg(7, 0x1234FEDC);
    start_cpu();

    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 0xFFFFFF80u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(2), 0xFFFF8001u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(4), 0x80u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(6), 0xFEDCu);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 0x80563412u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(2), 0x00000180u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(4), 0xFFFF80ABu);
}

TEST_F(CortexM3Test, LoadStoreWideWordAndHalfwordImmediateOffsets) {
    load_program({
        0xF8C3,
        0x2010, // str.w r2, [r3, #16]
        0xF8D3,
        0x4010, // ldr.w r4, [r3, #16]
        0xF8A5,
        0x6008, // strh.w r6, [r5, #8]
        0xF8B5,
        0x7008, // ldrh.w r7, [r5, #8]
    });
    reset_cpu();
    set_reg(2, 0x12345678);
    set_reg(3, 0x200);
    set_reg(5, 0x240);
    set_reg(6, 0xABCD);
    start_cpu();

    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(4), 0x12345678u);
    EXPECT_EQ(reg(7), 0xABCDu);
}

TEST_F(CortexM3Test, LdrWidePcRelativeLiteralPool) {
    // Exact Keil/F103 opcode that used to fault: ldr.w r9, [pc, #0x1B8]
    // (F8DF 91B8). Program at addr 0 → literal at Align(0+4,4)+0x1B8 = 0x1BC.
    load_program({0xF8DF, 0x91B8});
    uint32_t literal = 0x40010800u; // a typical `=const` value (GPIO base)
    ASSERT_TRUE(
        mem_.load(0x1BC, {reinterpret_cast<const uint8_t*>(&literal), 4})
            .has_value());
    reset_cpu();
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(9), 0x40010800u);
}

TEST_F(CortexM3Test, LdrWidePcRelativeLiteralSmallOffset) {
    // ldr.w r4, [pc, #8]: hw2 = Rt<<12 | imm12 = 0x4008.
    // Program at 0 → addr = Align(0+4,4)+8 = 0xC.
    load_program({0xF8DF, 0x4008});
    uint32_t literal = 0xDEADBEEFu;
    ASSERT_TRUE(
        mem_.load(0x0C, {reinterpret_cast<const uint8_t*>(&literal), 4})
            .has_value());
    reset_cpu();
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(4), 0xDEADBEEFu);
}

TEST_F(CortexM3Test, BlxRegisterSetsLinkRegister) {
    // 0x4780 = blx r0. Program at addr 0 → LR = (0+2)|1 = 3 (Thumb bit set),
    // and PC branches to r0 & ~1. This mirrors a Keil Reset_Handler that does
    // `blx r0` to call SystemInit, where SystemInit returns via `bx lr`.
    load_program({0x4780});
    reset_cpu();
    set_reg(0, 0x100u | 1u); // Thumb target
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(14), 3u);     // LR = return address | Thumb
    EXPECT_EQ(reg(15), 0x100u); // PC = target & ~1
}

TEST_F(CortexM3Test, BxRegisterDoesNotClobberLinkRegister) {
    // 0x4700 = bx r0 (bit[7]=0) — must NOT touch LR, only branch.
    load_program({0x4700});
    reset_cpu();
    set_reg(0, 0x200u | 1u);
    set_reg(14, 0x5555u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(14), 0x5555u); // LR untouched
    EXPECT_EQ(reg(15), 0x200u);
}

TEST_F(CortexM3Test, AdrPcRelative) {
    // 0xA00A = adr r0, pc+40. Program at 0 → r0 = Align(0+4,4)+40 = 0x2C.
    // This is the exact Keil __scatterload_rt2 opcode that used to fault.
    load_program({0xA00A});
    reset_cpu();
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 0x2Cu);
}

TEST_F(CortexM3Test, AddRdSpImmediate) {
    // 0xA804 = add r0, sp, #(4*4=16). SP=0x100 → r0 = 0x110.
    load_program({0xA804});
    reset_cpu();
    set_reg(13, 0x100u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 0x110u);
}

TEST_F(CortexM3Test, SubwPlainImm12) {
    // 0xF2A1 0009 = subw r0, r1, #9 (plain imm12). r1=20 → r0=11.
    load_program({0xF2A1, 0x0009});
    reset_cpu();
    set_reg(1, 20u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 11u);
}

TEST_F(CortexM3Test, AddwPlainImm12) {
    // 0xF201 0009 = addw r0, r1, #9. r1=5 → r0=14.
    load_program({0xF201, 0x0009});
    reset_cpu();
    set_reg(1, 5u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 14u);
}

TEST_F(CortexM3Test, StrbWideImm12Offset) {
    // 0xF880 1D14 = strb.w r1, [r0, #0xD14] (hw1[7]=1 → imm12 offset).
    // The exact F103 SystemClock_Config opcode. r0=0, r1=0xAB → byte at 0xD14.
    load_program({0xF880, 0x1D14});
    reset_cpu();
    set_reg(0, 0u);
    set_reg(1, 0xABu);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    auto v = bus_.read(0xD14u, Width::Byte);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 0xABu);
}

TEST_F(CortexM3Test, LdrbWideNegativeOffset) {
    // 0xF811 0C14 = ldrb.w r0, [r1, #-20] (hw1[7]=0, op=C). r1=0x100 → [0xEC].
    load_program({0xF811, 0x0C14});
    reset_cpu();
    set_reg(1, 0x100u);
    uint8_t seed = 0x5A;
    ASSERT_TRUE(mem_.load(0xECu, {&seed, 1}).has_value());
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 0x5Au);
}

TEST_F(CortexM3Test, StrbWidePreIndexNegative) {
    // 0xF801 0D14 = strb.w r0, [r1, #-0x14]! (hw1[7]=0, op=D pre-index neg).
    // r1=0x100, r0=0x77 → store byte at 0xEC, then r1=0xEC.
    load_program({0xF801, 0x0D14});
    reset_cpu();
    set_reg(0, 0x77u);
    set_reg(1, 0x100u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(1), 0xECu);
    auto v = bus_.read(0xECu, Width::Byte);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 0x77u);
}

TEST_F(CortexM3Test, CmpWideShiftedRegDoesNotWritePc) {
    // 0xEBB0 0F81 = cmp.w r0, r1, lsl #2 (Rd=15, S=1 → flags only).
    // The F103 HAL_RCC_ClockConfig PLL-wait opcode. r0=10, r1=3 → 10-(3<<2)=-2.
    // Must update flags WITHOUT writing PC.
    load_program({0xEBB0, 0x0F81});
    reset_cpu();
    set_reg(0, 10u);
    set_reg(1, 3u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(15), 4u); // advanced past the 32-bit insn, PC not corrupted
}

TEST_F(CortexM3Test, OrrRegisterUsesBits5to3AsRm) {
    // 0x4301 = orrs r1, r0 (Rm=R0=bits[5:3], Rd=R1=bits[2:0]).
    // Regression for the data-proc-register Rm field bug (was reading bits[8:6]).
    load_program({0x4301});
    reset_cpu();
    set_reg(0, 0x0Fu);
    set_reg(1, 0xF0u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(1), 0xFFu);
}

TEST_F(CortexM3Test, AndRegisterUsesBits5to3AsRm) {
    // 0x4001 = ands r1, r0. r1=0xFF, r0=0x0F → r1=0x0F.
    load_program({0x4001});
    reset_cpu();
    set_reg(0, 0x0Fu);
    set_reg(1, 0xFFu);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(1), 0x0Fu);
}

TEST_F(CortexM3Test, BranchToSelfStaysInPlace) {
    // 0xE7FE = b . (branch to self). PC must NOT advance — the step loop
    // must not mistake a self-branch for sequential fall-through.
    load_program({0xE7FE});
    reset_cpu();
    set_pc(0);
    start_cpu();
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(15), 0u); // still at 0, not 2/4/6...
}

TEST_F(CortexM3Test, SignedDivisionUsesSignedOperands) {
    load_program({
        0xFBB1,
        0xF0F2, // udiv r0, r1, r2
        0xFB94,
        0xF3F5, // sdiv r3, r4, r5
    });
    reset_cpu();
    set_reg(1, 10);
    set_reg(2, 3);
    set_reg(4, static_cast<data_t>(-9));
    set_reg(5, 2);
    start_cpu();

    ASSERT_TRUE(cpu_->step().has_value());
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 3u);
    EXPECT_EQ(reg(3), static_cast<data_t>(-4));
}

TEST_F(CortexM3Test, BitfieldInstructions) {
    load_program({
        0xF361,
        0x200F, // bfi r0, r1, #8, #8
        0xF36F,
        0x120F, // bfc r2, #4, #12
        0xF3C4,
        0x1345, // ubfx r3, r4, #5, #6
        0xF346,
        0x15C7, // sbfx r5, r6, #7, #8
    });
    reset_cpu();
    set_reg(0, 0xFFFF0000);
    set_reg(1, 0xAB);
    set_reg(2, 0xFFFFFFFF);
    set_reg(4, 0x000006A0);
    set_reg(6, 0x00004000);
    start_cpu();

    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(0), 0xFFFFAB00u);
    EXPECT_EQ(reg(2), 0xFFFF000Fu);
    EXPECT_EQ(reg(3), 0x35u);
    EXPECT_EQ(reg(5), 0xFFFFFF80u);
}

TEST_F(CortexM3Test, MultiplyAccumulateAndLongMultiply) {
    load_program({
        0xFB01,
        0x3002, // mla r0, r1, r2, r3
        0xFB05,
        0x7416, // mls r4, r5, r6, r7
        0xFBA2,
        0x0103, // umull r0, r1, r2, r3
        0xFB86,
        0x4507, // smull r4, r5, r6, r7
    });
    reset_cpu();
    set_reg(1, 6);
    set_reg(2, 7);
    set_reg(3, 5);
    set_reg(5, 6);
    set_reg(6, static_cast<data_t>(-2));
    set_reg(7, 50);
    start_cpu();

    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 47u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(4), 62u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 35u);
    EXPECT_EQ(reg(1), 0u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(4), static_cast<data_t>(-100));
    EXPECT_EQ(reg(5), 0xFFFFFFFFu);
}

TEST_F(CortexM3Test, ModifiedImmediateAdcSbcReadCarryFlag) {
    load_program({
        0xF386,
        0x8800, // msr apsr_nzcvq, r6
        0xF141,
        0x0001, // adc.w r0, r1, #1
        0xF163,
        0x0201, // sbc.w r2, r3, #1
    });
    reset_cpu();
    set_reg(1, 5);
    set_reg(3, 5);
    set_reg(6, PSR_C);
    start_cpu();

    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(0), 7u);
    EXPECT_EQ(reg(2), 4u);
}

TEST_F(CortexM3Test, ItBlockConditionallyExecutesFollowingInstructions) {
    load_program({
        0x2801, // cmp r0, #1 -> Z=0
        0xBF08, // it eq
        0x2107, // movs r1, #7 (skipped)
        0xBF18, // it ne
        0x2209, // movs r2, #9 (executed)
    });
    reset_cpu();
    set_reg(0, 0);
    start_cpu();

    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(1), 0u);
    EXPECT_EQ(reg(2), 9u);
}

TEST_F(CortexM3Test, ConditionalWideBranch) {
    load_program({
        0x2800, // cmp r0, #0 -> Z=1
        0xF000,
        0x8001, // beq.w +2 halfwords -> target at 0x08
        0x2101, // movs r1, #1 (skipped)
        0x2202, // movs r2, #2
    });
    reset_cpu();
    set_reg(0, 0);
    start_cpu();

    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(1), 0u);
    EXPECT_EQ(reg(2), 2u);
}

TEST_F(CortexM3Test, TbbUsesPcPlusFourAsBranchBase) {
    load_program({
        0xE8DF,
        0xF000, // tbb [pc, r0]
        0x0402, // table bytes: r0=0 -> 0x08, r0=1 -> 0x0C
        0x2163, // movs r1, #99 (skipped)
        0x2101, // target0: movs r1, #1
        0xE000, // b done
        0x2102, // target1: movs r1, #2
        0xBF00, // done: nop
    });
    reset_cpu();
    set_reg(0, 1);
    start_cpu();

    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(cpu_->pc().value_or(0), 0x0Cu);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(1), 2u);
}
