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

TEST_F(CortexM3Test, LoadStoreWideRegisterOffset) {
    // Register offset (T2): addr = Rn + (Rm << shift2). The #9 bug treated
    // hw2[7:0] as imm8, computing r1+2 instead of r1+r2.
    //   str.w r0,[r1,r2]       = F841 0002  → r1 + r2
    //   ldr.w r4,[r1,r2,lsl#3] = F851 4032  → r1 + (r2<<3)
    load_program({0xF841, 0x0002, 0xF851, 0x4032});
    reset_cpu();
    set_reg(1, 0x100u);
    set_reg(2, 0x40u);
    set_reg(0, 0x12345678u);
    uint32_t lit = 0xCAFEF00Du;
    ASSERT_TRUE(mem_.load(0x300u, {reinterpret_cast<const uint8_t*>(&lit), 4})
                    .has_value());
    start_cpu();

    ASSERT_TRUE(cpu_->step().has_value()); // str.w r0,[r1,r2] → 0x140
    auto w = bus_.read(0x140u, Width::Word);
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(*w, 0x12345678u);
    // Regression guard: the buggy imm8 path wrote to r1+2 = 0x102 instead.
    auto bad = bus_.read(0x102u, Width::Word);
    ASSERT_TRUE(bad.has_value());
    EXPECT_NE(*bad, 0x12345678u);

    ASSERT_TRUE(cpu_->step().has_value()); // ldr.w r4,[r1,r2,lsl#3] → 0x300
    EXPECT_EQ(reg(4), 0xCAFEF00Du);
}

TEST_F(CortexM3Test, LoadStoreWideRegisterOffsetByteHalf) {
    //   strb.w r0,[r1,r2] = F801 0002 (byte → r1+r2)
    //   ldrh.w r4,[r1,r3] = F831 4003 (half ← r1+r3)
    load_program({0xF801, 0x0002, 0xF831, 0x4003});
    reset_cpu();
    set_reg(1, 0x100u);
    set_reg(2, 0x10u); // strb → 0x110
    set_reg(3, 0x40u); // ldrh → 0x140
    set_reg(0, 0xABu);
    uint16_t lit = 0xBEEFu;
    ASSERT_TRUE(mem_.load(0x140u, {reinterpret_cast<const uint8_t*>(&lit), 2})
                    .has_value());
    start_cpu();

    ASSERT_TRUE(cpu_->step().has_value()); // strb.w → 0x110
    auto b = bus_.read(0x110u, Width::Byte);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(*b, 0xABu);
    ASSERT_TRUE(cpu_->step().has_value()); // ldrh.w r4,[r1,r3] → 0x140
    EXPECT_EQ(reg(4), 0xBEEFu);
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

// ── T1 静默错误修复单测(coverage matrix §2 #2–#11)──

TEST_F(CortexM3Test, OrnWideRegisterIncludesRn) {
    // #2: orn.w r0,r1,r2 (ea61 0002) = r1 | ~r2; bug gave ~shifted (dropped Rn).
    load_program({0xEA61, 0x0002});
    reset_cpu();
    set_reg(1, 0x000000FFu);
    set_reg(2, 0x0000000Fu);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 0xFFFFFFFFu); // 0xFF | ~0x0F
}

TEST_F(CortexM3Test, RsbsWideCarryReflectsMinuend) {
    // #3: rsbs r0,r1,#5 (f1d1 0005) = 5 - r1; r1=3 → C=1 (5>=3). mrs r2,apsr.
    load_program({0xF1D1, 0x0005, 0xF3EF, 0x8200});
    reset_cpu();
    set_reg(1, 3u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value()); // rsbs
    ASSERT_TRUE(cpu_->step().has_value()); // mrs r2,apsr
    EXPECT_EQ(reg(0), 2u);
    EXPECT_EQ(reg(2), 0x20000000u); // C set; bug gave C=(rn>=imm)=0
}

TEST_F(CortexM3Test, ShiftBy32InShiftedRegisterOperand) {
    // #4: add.w r0,r1,r2,lsr #32 (eb01 0012); LSR#32 → shifted=0.
    load_program({0xEB01, 0x0012});
    reset_cpu();
    set_reg(1, 0x10u);
    set_reg(2, 0xFFFFFFFFu);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 0x10u); // r1 + 0; bug gave r1 + r2
}

TEST_F(CortexM3Test, CpsFControlsFaultMaskNotPrimask) {
    // #5: cpsid f (b671) → FAULTMASK, not PRIMASK.
    load_program({0xB671, 0xF3EF, 0x8013, 0xF3EF, 0x8110}); // cpsid f; mrs r0,faultmask; mrs r1,primask
    reset_cpu();
    start_cpu();
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(0), 1u); // faultmask set
    EXPECT_EQ(reg(1), 0u); // primask untouched (bug had set primask)
}

TEST_F(CortexM3Test, BkptIsNotSilentlyNopped) {
    // #6: bkpt #5 (be05) → HardFault entry (PC leaves the bkpt), not a NOP
    // that simply advances PC to 2.
    load_program({0xBE05});
    reset_cpu();
    start_cpu();
    [[maybe_unused]] auto _ = cpu_->step();
    EXPECT_NE(cpu_->pc().value_or(0xDEAD), 2u);
}

TEST_F(CortexM3Test, MulWideRa15DoesNotFoldPc) {
    // #7: mul.w r0,r1,r2 (fb01 f002); Ra=15 → no accumulate; bug added raw PC.
    load_program({0xFB01, 0xF002});
    reset_cpu();
    set_reg(1, 3u);
    set_reg(2, 5u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 15u);
}

TEST_F(CortexM3Test, SubwPcUsesAlignedPcPlusFour) {
    // #8: subw r0,pc,#4 (f2af 0004) at PC=0; base=Align(PC+4,4)=4 → r0=0.
    // bug used raw PC=0 → r0=0xFFFFFFFC.
    load_program({0xF2AF, 0x0004});
    reset_cpu();
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 0u);
}

TEST_F(CortexM3Test, TbhDispatchesViaTableBranchHandler) {
    // #10: tbh [pc,r0,lsl#1] (e8df f010); H-bit must not misroute to LDRD.
    // r0=0, table halfword at PC+4 = 0 → target = PC+4.
    load_program({0xE8DF, 0xF010});
    reset_cpu();
    set_reg(0, 0u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(cpu_->pc().value_or(0), 4u);
}

TEST_F(CortexM3Test, LdrexStrexBehaveAsPlainLoadStore) {
    // #11: ldrex r0,[r1] (e851 0f00) → plain load; strex r3,r2,[r1] (e841 2300)
    // → plain store + Rd=0 (no monitor → always succeeds).
    load_program({0xE851, 0x0F00, 0xE841, 0x2300});
    uint32_t seed = 0xDEADBEEFu;
    ASSERT_TRUE(mem_.load(0x100u, {reinterpret_cast<const uint8_t*>(&seed), 4})
                    .has_value());
    reset_cpu();
    set_reg(1, 0x100u);
    set_reg(2, 0xCAFEu);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value()); // ldrex
    EXPECT_EQ(reg(0), 0xDEADBEEFu);
    ASSERT_TRUE(cpu_->step().has_value()); // strex
    EXPECT_EQ(reg(3), 0u); // success status
    auto v = bus_.read(0x100u, Width::Word);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 0xCAFEu);
}

// ── T2 缺失指令单测(coverage matrix §3)──

TEST_F(CortexM3Test, OrnMvnWideImmediate) {
    // orn.w r3,r1,#0x11 (f061 0311)=r1|~imm; mvn.w r0,#0x11 (f06f 0011)=~imm.
    load_program({0xF061, 0x0311, 0xF06F, 0x0011});
    reset_cpu();
    set_reg(1, 0x000000FFu);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value()); // orn → 0xFF | ~0x11
    EXPECT_EQ(reg(3), 0xFFFFFFFFu);
    ASSERT_TRUE(cpu_->step().has_value()); // mvn → ~0x11
    EXPECT_EQ(reg(0), 0xFFFFFFEEu);
}

TEST_F(CortexM3Test, RorRrxShiftedRegister) {
    // mov.w r0,r1,ror#4 (ea4f 1031); msr apsr,r2 (set C); mov.w r3,r1,rrx (ea4f 0331).
    load_program({0xEA4F, 0x1031, 0xF382, 0x8800, 0xEA4F, 0x0331});
    reset_cpu();
    set_reg(1, 0x12345678u);
    set_reg(2, 0x20000000u); // PSR_C
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value()); // ror#4
    EXPECT_EQ(reg(0), 0x81234567u);
    ASSERT_TRUE(cpu_->step().has_value()); // msr sets C
    ASSERT_TRUE(cpu_->step().has_value()); // rrx: (C<<31)|(r1>>1)
    EXPECT_EQ(reg(3), 0x891A2B3Cu);
}

TEST_F(CortexM3Test, SmlalUmlalWideAccumulate) {
    // smlal r0,r1,r2,r3 (fbc2 0103); umlal r0,r1,r2,r3 (fbe2 0103).
    // r2=r3=0xFFFFFFFF: signed product=1, unsigned=0xFFFFFFFE00000001.
    load_program({0xFBC2, 0x0103, 0xFBE2, 0x0103});
    reset_cpu();
    set_reg(0, 0u);
    set_reg(1, 0u);
    set_reg(2, 0xFFFFFFFFu);
    set_reg(3, 0xFFFFFFFFu);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value()); // smlal: 0 + 1
    EXPECT_EQ(reg(0), 1u);
    EXPECT_EQ(reg(1), 0u);
    ASSERT_TRUE(cpu_->step().has_value()); // umlal: 1 + 0xFFFFFFFE00000001
    EXPECT_EQ(reg(0), 2u);
    EXPECT_EQ(reg(1), 0xFFFFFFFEu);
}

TEST_F(CortexM3Test, LdrsbLdrshWideSignExtend) {
    // ldrsb.w r0,[r1,#4] (f991 0004); ldrsh.w r0,[r1,#8] (f9b1 0008).
    load_program({0xF991, 0x0004, 0xF9B1, 0x0008});
    uint8_t b = 0x80;
    uint16_t h = 0x8000;
    ASSERT_TRUE(mem_.load(0x104u, {&b, 1}).has_value());
    ASSERT_TRUE(mem_.load(0x108u, {reinterpret_cast<uint8_t*>(&h), 2})
                    .has_value());
    reset_cpu();
    set_reg(1, 0x100u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value()); // ldrsb → 0xFFFFFF80
    EXPECT_EQ(reg(0), 0xFFFFFF80u);
    ASSERT_TRUE(cpu_->step().has_value()); // ldrsh → 0xFFFF8000
    EXPECT_EQ(reg(0), 0xFFFF8000u);
}

TEST_F(CortexM3Test, ClzRbitRevWide) {
    // clz r0,r1 (fab1 f081); rbit r0,r1 (fa91 f0a1); rev.w r0,r1 (fa91 f081).
    load_program({0xFAB1, 0xF081, 0xFA91, 0xF0A1, 0xFA91, 0xF081});
    reset_cpu();
    set_reg(1, 0x00010000u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value()); // clz (bit16 → 15 leading zeros)
    EXPECT_EQ(reg(0), 15u);
    ASSERT_TRUE(cpu_->step().has_value()); // rbit (bit16 → bit15)
    EXPECT_EQ(reg(0), 0x00008000u);
    ASSERT_TRUE(cpu_->step().has_value()); // rev.w (byte-reverse)
    EXPECT_EQ(reg(0), 0x00000100u);
}

TEST_F(CortexM3Test, SsatUsatWideSaturation) {
    // ssat r0,#5,r1 (f301 0004): 100→15,Q; usat r2,#5,r1 (f381 0205): 100→31,Q.
    // mrs r3,apsr (f3ef 8300) reads Q.
    load_program({0xF301, 0x0004, 0xF381, 0x0205, 0xF3EF, 0x8300});
    reset_cpu();
    set_reg(1, 100u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value()); // ssat → 15
    EXPECT_EQ(reg(0), 15u);
    ASSERT_TRUE(cpu_->step().has_value()); // usat → 31
    EXPECT_EQ(reg(2), 31u);
    ASSERT_TRUE(cpu_->step().has_value()); // mrs r3,apsr
    EXPECT_NE(reg(3) & 0x08000000u, 0u); // Q set
}

TEST_F(CortexM3Test, ClrexNopWideAreNoop) {
    // clrex (f3bf 8f2f); nop.w (f3af 8000) — both advance PC, no fault.
    load_program({0xF3BF, 0x8F2F, 0xF3AF, 0x8000});
    reset_cpu();
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value()); // clrex PC 0→4
    EXPECT_EQ(cpu_->pc().value_or(0), 4u);
    ASSERT_TRUE(cpu_->step().has_value()); // nop.w PC 4→8
    EXPECT_EQ(cpu_->pc().value_or(0), 8u);
}

TEST_F(CortexM3Test, McrMrcCoprocessorFaults) {
    // mrc p15 (ee11 0f10) → no coprocessor → IllegalInstruction.
    load_program({0xEE11, 0x0F10});
    reset_cpu();
    start_cpu();
    EXPECT_FALSE(cpu_->step().has_value()); // faults
}

// ── Flag sweep: data-proc (shifted register) N/Z/C/V (matrix §5 gap) ──
// T1a fixed shifter-carry feeding C; these cover the *arithmetic* flag update
// path after a shifted operand (ADD carry/overflow, SUB borrow). Flags read via
// `MRS R0, APSR` (0xF3EF 0x8000): N=31, Z=30, C=29, V=28.

TEST_F(CortexM3Test, AddsShiftedRegSetsCarryOverflowClearsN) {
    // eb11 1302 = adds.w r3, r1, r2, lsl #4.
    // 0x80000000 + (0x08000000 << 4 = 0x80000000) = 0x1_00000000 → r3=0,
    // C=1 (carry out), V=1 (signed -2^31 + -2^31 overflow), Z=1, N=0.
    load_program({0xEB11, 0x1302, 0xF3EF, 0x8000});
    reset_cpu();
    set_reg(1, 0x80000000u);
    set_reg(2, 0x08000000u);
    start_cpu();
    step_cpu(); // adds.w
    step_cpu(); // mrs r0, apsr
    EXPECT_EQ(reg(3), 0u);
    EXPECT_NE(reg(0) & (1u << 29), 0u) << "carry out sets C";
    EXPECT_NE(reg(0) & (1u << 28), 0u) << "signed overflow sets V";
    EXPECT_NE(reg(0) & (1u << 30), 0u) << "zero result sets Z";
    EXPECT_EQ(reg(0) & (1u << 31), 0u) << "N clear";
}

TEST_F(CortexM3Test, SubsShiftedRegBorrowClearsCarrySetsN) {
    // ebb1 0392 = subs.w r3, r1, r2, lsr #2.
    // 0x10 - (0x100 >> 2 = 0x40) = -0x30 → r3=0xFFFFFFD0,
    // C=0 (borrow), N=1, Z=0, V=0 (in range).
    load_program({0xEBB1, 0x0392, 0xF3EF, 0x8000});
    reset_cpu();
    set_reg(1, 0x10u);
    set_reg(2, 0x100u);
    start_cpu();
    step_cpu();
    step_cpu();
    EXPECT_EQ(reg(3), 0xFFFFFFD0u);
    EXPECT_EQ(reg(0) & (1u << 29), 0u) << "borrow clears C";
    EXPECT_NE(reg(0) & (1u << 31), 0u) << "negative result sets N";
    EXPECT_EQ(reg(0) & (1u << 30), 0u) << "Z clear";
}

TEST_F(CortexM3Test, AddsRorOperandComputesResultAndFlags) {
    // eb11 1332 = adds.w r3, r1, r2, ror #4.
    // 0 + (0x10000000 ror 4 = 0x01000000) = 0x01000000; no carry/overflow.
    load_program({0xEB11, 0x1332, 0xF3EF, 0x8000});
    reset_cpu();
    set_reg(1, 0u);
    set_reg(2, 0x10000000u);
    start_cpu();
    step_cpu();
    step_cpu();
    EXPECT_EQ(reg(3), 0x01000000u);
    EXPECT_EQ(reg(0) & (1u << 29), 0u) << "no carry";
    EXPECT_EQ(reg(0) & (1u << 28), 0u) << "no overflow";
    EXPECT_EQ(reg(0) & (1u << 31), 0u) << "N clear";
    EXPECT_EQ(reg(0) & (1u << 30), 0u) << "Z clear";
}
