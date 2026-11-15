#include "test_cortex_m3_common.hpp"

// ── T1 静默错误修复单测(coverage matrix §2 #2–#11)──

TEST_F(CortexM3Test, OrnWideRegisterIncludesRn) {
    // #2: orn.w r0,r1,r2 (ea61 0002) = r1 | ~r2; bug gave ~shifted (dropped
    // Rn).
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
    load_program(
        {0xB671, 0xF3EF, 0x8013, 0xF3EF, 0x8110}); // cpsid f; mrs r0,faultmask; mrs r1,primask
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
    ASSERT_TRUE(mem_.copy_in(0x100u, {reinterpret_cast<const uint8_t*>(&seed), 4}));
    reset_cpu();
    set_reg(1, 0x100u);
    set_reg(2, 0xCAFEu);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value()); // ldrex
    EXPECT_EQ(reg(0), 0xDEADBEEFu);
    ASSERT_TRUE(cpu_->step().has_value()); // strex
    EXPECT_EQ(reg(3), 0u);                 // success status
    auto v = access_.read(0x100u, Width::kWord);
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
    // mov.w r0,r1,ror#4 (ea4f 1031); msr apsr,r2 (set C); mov.w r3,r1,rrx (ea4f
    // 0331).
    load_program({0xEA4F, 0x1031, 0xF382, 0x8800, 0xEA4F, 0x0331});
    reset_cpu();
    set_reg(1, 0x12345678u);
    set_reg(2, 0x20000000u); // kPsrC
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
    ASSERT_TRUE(mem_.copy_in(0x104u, {&b, 1}));
    ASSERT_TRUE(mem_.copy_in(0x108u, {reinterpret_cast<uint8_t*>(&h), 2}));
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
    EXPECT_NE(reg(3) & 0x08000000u, 0u);   // Q set
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

// ── ADC.W/SBC.W (shifted register) — matrix F32-6 gap (Tier-1a) ──
// op 10/11 were absent from t32_dataproc_reg's switch → IllegalInstruction.
// Flags use a carry-in-aware path (set_adc_flags/set_sbc_flags) so the
// 0xFFFFFFFF+C edge reports C correctly. Encodings via objdump -mcpu=cortex-m3.

TEST_F(CortexM3Test, AdcShiftedRegCarryOutAndZero) {
    // eb51 1302 = adcs.w r3, r1, r2, lsl #4. 0xFFFFFFFF + 0 + C(1) = 0;
    // C=1 (carry-out), Z=1, N=0, V=0.
    load_program({0xF386, 0x8800, 0xEB51, 0x1302, 0xF3EF, 0x8000});
    reset_cpu();
    set_reg(1, 0xFFFFFFFFu);
    set_reg(2, 0u);
    set_reg(6, 1u << 29); // C=1
    start_cpu();
    step_cpu();
    step_cpu();
    step_cpu();
    EXPECT_EQ(reg(3), 0u);
    EXPECT_NE(reg(0) & (1u << 29), 0u) << "carry-out sets C";
    EXPECT_NE(reg(0) & (1u << 30), 0u) << "zero result sets Z";
    EXPECT_EQ(reg(0) & (1u << 31), 0u) << "N clear";
    EXPECT_EQ(reg(0) & (1u << 28), 0u) << "V clear";
}

TEST_F(CortexM3Test, AdcShiftedRegNoShiftSignedOverflow) {
    // eb51 0302 = adcs.w r3, r1, r2 (no shift). C=0 at reset.
    // 0x7FFFFFFF + 1 + 0 = 0x80000000; V=1 (pos+pos→neg), N=1, C=0, Z=0.
    load_program({0xEB51, 0x0302, 0xF3EF, 0x8000});
    reset_cpu();
    set_reg(1, 0x7FFFFFFFu);
    set_reg(2, 1u);
    start_cpu();
    step_cpu();
    step_cpu();
    EXPECT_EQ(reg(3), 0x80000000u);
    EXPECT_NE(reg(0) & (1u << 31), 0u) << "negative result sets N";
    EXPECT_NE(reg(0) & (1u << 28), 0u) << "signed overflow sets V";
    EXPECT_EQ(reg(0) & (1u << 29), 0u) << "no carry-out";
    EXPECT_EQ(reg(0) & (1u << 30), 0u) << "Z clear";
}

TEST_F(CortexM3Test, SbcShiftedRegNoBorrowKeepsCarry) {
    // eb71 0302 = sbcs.w r3, r1, r2. C=1 (no borrow subtracted).
    // 5 - 1 - !C(0) = 4; C stays 1 (no borrow), N=0, Z=0, V=0.
    load_program({0xF386, 0x8800, 0xEB71, 0x0302, 0xF3EF, 0x8000});
    reset_cpu();
    set_reg(1, 5u);
    set_reg(2, 1u);
    set_reg(6, 1u << 29);
    start_cpu();
    step_cpu();
    step_cpu();
    step_cpu();
    EXPECT_EQ(reg(3), 4u);
    EXPECT_NE(reg(0) & (1u << 29), 0u) << "no borrow keeps C set";
    EXPECT_EQ(reg(0) & (1u << 31), 0u) << "N clear";
    EXPECT_EQ(reg(0) & (1u << 28), 0u) << "V clear";
}

TEST_F(CortexM3Test, SbcShiftedRegBorrowClearsCarry) {
    // eb71 03d2 = sbcs.w r3, r1, r2, lsr #3. C=1.
    // 0x10 - (0xFF >> 3 = 0x1F) - 0 = 0xFFFFFFF1; C=0 (borrow), N=1, V=0.
    load_program({0xF386, 0x8800, 0xEB71, 0x03D2, 0xF3EF, 0x8000});
    reset_cpu();
    set_reg(1, 0x10u);
    set_reg(2, 0xFFu);
    set_reg(6, 1u << 29);
    start_cpu();
    step_cpu();
    step_cpu();
    step_cpu();
    EXPECT_EQ(reg(3), 0xFFFFFFF1u);
    EXPECT_EQ(reg(0) & (1u << 29), 0u) << "borrow clears C";
    EXPECT_NE(reg(0) & (1u << 31), 0u) << "negative result sets N";
    EXPECT_EQ(reg(0) & (1u << 28), 0u) << "V clear (in range)";
}

// ── ADCS/SBCS (16-bit register) — matrix F16-3 gap (Tier-1b) ──
// op 5/6 computed the right result but only called update_nz; C/V never set.

TEST_F(CortexM3Test, Adcs16BitSetsCarryAndZero) {
    // 414b = adcs r3, r1 → R3 = R3 + R1 + C. C=1.
    // 0xFFFFFFFF + 0 + 1 = 0; C=1, Z=1, N=0, V=0.
    load_program({0xF386, 0x8800, 0x414B, 0xF3EF, 0x8000});
    reset_cpu();
    set_reg(3, 0xFFFFFFFFu); // Rdn (source + dest)
    set_reg(1, 0u);          // Rm
    set_reg(6, 1u << 29);
    start_cpu();
    step_cpu();
    step_cpu();
    step_cpu();
    EXPECT_EQ(reg(3), 0u);
    EXPECT_NE(reg(0) & (1u << 29), 0u) << "carry-out sets C";
    EXPECT_NE(reg(0) & (1u << 30), 0u) << "zero result sets Z";
    EXPECT_EQ(reg(0) & (1u << 28), 0u) << "V clear";
}

TEST_F(CortexM3Test, Sbcs16BitBorrowClearsCarry) {
    // 418b = sbcs r3, r1 → R3 = R3 - R1 - !C. C=1.
    // 5 - 7 - 0 = 0xFFFFFFFE; C=0 (borrow), N=1, V=0.
    load_program({0xF386, 0x8800, 0x418B, 0xF3EF, 0x8000});
    reset_cpu();
    set_reg(3, 5u);
    set_reg(1, 7u);
    set_reg(6, 1u << 29);
    start_cpu();
    step_cpu();
    step_cpu();
    step_cpu();
    EXPECT_EQ(reg(3), 0xFFFFFFFEu);
    EXPECT_EQ(reg(0) & (1u << 29), 0u) << "borrow clears C";
    EXPECT_NE(reg(0) & (1u << 31), 0u) << "negative sets N";
    EXPECT_EQ(reg(0) & (1u << 28), 0u) << "V clear";
}

// ── SDIV/UDIV divide-by-zero + overflow (Tier-1c) ──
// ARMv7-M (CCR.DIV_0_TRP=0, reset default): UDIV/0→0; SDIV/0→0 if dividend≥0
// else INT_MIN; INT_MIN/-1 saturates to INT_MIN (C UB otherwise).

TEST_F(CortexM3Test, SdivByZeroNegativeYieldsZero) {
    // fb91 f3f2 = sdiv r3, r1, r2. Cortex-M3 SDIV/0 → 0 for BOTH signs
    // (confirmed vs qemu-system-arm mps2-an385, notes 017). The substantive
    // fix in this batch is the INT_MIN/-1 saturation guard below.
    load_program({0xFB91, 0xF3F2});
    reset_cpu();
    set_reg(1, 0xFFFFFFFFu); // -1
    set_reg(2, 0u);
    start_cpu();
    step_cpu();
    EXPECT_EQ(reg(3), 0u);
}

TEST_F(CortexM3Test, SdivByZeroNonNegativeYieldsZero) {
    load_program({0xFB91, 0xF3F2});
    reset_cpu();
    set_reg(1, 5u);
    set_reg(2, 0u);
    start_cpu();
    step_cpu();
    EXPECT_EQ(reg(3), 0u);
}

TEST_F(CortexM3Test, SdivIntMinOverMinusOneSaturates) {
    // fb91 f3f2 = sdiv r3, r1, r2. INT_MIN / -1 saturates to INT_MIN.
    load_program({0xFB91, 0xF3F2});
    reset_cpu();
    set_reg(1, 0x80000000u);
    set_reg(2, 0xFFFFFFFFu);
    start_cpu();
    step_cpu();
    EXPECT_EQ(reg(3), 0x80000000u);
}

TEST_F(CortexM3Test, SdivNormalRoundsTowardZero) {
    load_program({0xFB91, 0xF3F2});
    reset_cpu();
    set_reg(1, 100u);
    set_reg(2, 7u);
    start_cpu();
    step_cpu();
    EXPECT_EQ(reg(3), 14u);
}

TEST_F(CortexM3Test, UdivByZeroYieldsZero) {
    // fbb1 f3f2 = udiv r3, r1, r2. 123 / 0 → 0.
    load_program({0xFBB1, 0xF3F2});
    reset_cpu();
    set_reg(1, 123u);
    set_reg(2, 0u);
    start_cpu();
    step_cpu();
    EXPECT_EQ(reg(3), 0u);
}

TEST_F(CortexM3Test, Ac6AddwSubwDecodeDestinationIndependentlyOfImmediateBits) {
    // Exact instructions from nucleo_f103rb_uart_printf.ac6-O0.axf,
    // independently decoded by arm-none-eabi-objdump. The high halfword of
    // imm12 shares hw2 with Rd but must never select the destination register.
    load_program({
        0xF2A4,
        0x34F3, // SUBW r4, r4, #1011 (firmware PC 0x08000354)
        0xF208,
        0x38FD, // ADDW r8, r8, #1021 (firmware PC 0x08000450)
        0xF2A2,
        0x4233, // SUBW r2, r2, #1075 (firmware PC 0x08000506)
    });
    reset_cpu();
    set_reg(2, 2000);
    set_reg(3, 0xAABBCCDD);
    set_reg(4, 3000);
    set_reg(8, 4000);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(4), 1989u);
    EXPECT_EQ(reg(3), 0xAABBCCDDu);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(8), 5021u);
    EXPECT_EQ(reg(3), 0xAABBCCDDu);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(2), 925u);
    EXPECT_EQ(reg(4), 1989u);
    EXPECT_EQ(cpu_->pc().value(), 12u);
}

TEST_F(CortexM3Test, Ac6AdrWideWritesIpInsteadOfImmediateFieldRegister) {
    // Exact PC-relative formatter instructions at firmware 0x080020AE and
    // 0x080020CC. Here they run at 0x20/0x24 to exercise aligned PC+4 bases.
    load_program({0xF20F, 0x2C70, 0xF2AF, 0x0C70}, 0x20);
    reset_cpu();
    set_reg(0, 0x11111111);
    set_reg(2, 0x22222222);
    set_reg(12, 0xCCCCCCCC);
    set_pc(0x20);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(12), 0x294u); // Align(0x20+4,4) + 624
    EXPECT_EQ(reg(2), 0x22222222u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(12), 0xFFFFFFB8u); // Align(0x24+4,4) - 112
    EXPECT_EQ(reg(0), 0x11111111u);
    EXPECT_EQ(cpu_->pc().value(), 0x28u);
}

TEST_F(CortexM3Test, ThumbModifiedImmediateReplicatesBothLowAndHighHalfwords) {
    // Exact GNU assembler/objdump encodings, not assembled by the decoder's
    // field helpers: MOV/ORR/AND with the 00:imm8:00:imm8 replication form.
    load_program({
        0xF04F,
        0x10AB, // MOV.W r0, #0x00AB00AB
        0xF042,
        0x1155, // ORR.W r1, r2, #0x00550055
        0xF004,
        0x13FF, // AND.W r3, r4, #0x00FF00FF
        0xF04F,
        0x25AB, // MOV.W r5, #0xAB00AB00
        0xF04F,
        0x36AB, // MOV.W r6, #0xABABABAB
    });
    reset_cpu();
    set_reg(2, 0xAA00AA00);
    set_reg(4, 0x12345678);
    start_cpu();
    for (unsigned i = 0; i < 5; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(0), 0x00AB00ABu);
    EXPECT_EQ(reg(1), 0xAA55AA55u);
    EXPECT_EQ(reg(3), 0x00340078u);
    EXPECT_EQ(reg(5), 0xAB00AB00u);
    EXPECT_EQ(reg(6), 0xABABABABu);
}
