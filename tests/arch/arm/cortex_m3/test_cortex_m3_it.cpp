#include "test_cortex_m3_common.hpp"

#include <array>

TEST_F(CortexM3Test, Ac6RccNonNullConfigurationDoesNotReturnFromIttEqGuard) {
    // Real HAL_RCC_ClockConfig entry: CMP r0,#0; ITT EQ; MOVEQ r0,#1;
    // BXEQ LR. With non-null input both guarded instructions must be skipped.
    load_program({0x2800, 0xBF04, 0x2001, 0x4770, 0x212A});
    reset_cpu();
    set_reg(0, 0x200);
    set_reg(14, 0x101);
    start_cpu();
    for (unsigned i = 0; i < 5; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(0), 0x200u);
    EXPECT_EQ(reg(1), 42u);
    EXPECT_EQ(cpu_->pc().value(), 10u);
}

TEST_F(CortexM3Test, Ac6RccNullConfigurationReturnsErrorWithoutClobberingItFlags) {
    load_program({0x2800, 0xBF04, 0x2001, 0x4770, 0x212A});
    reset_cpu();
    set_reg(0, 0);
    set_reg(14, 0x101);
    start_cpu();
    for (unsigned i = 0; i < 4; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(0), 1u);
    EXPECT_EQ(reg(1), 0u);
    EXPECT_EQ(cpu_->pc().value(), 0x100u);
    // MOVEQ's 16-bit encoding does not alter the CMP flags inside IT.
    EXPECT_EQ(cpu_->xpsr() & (kPsrN | kPsrZ | kPsrC | kPsrV), kPsrZ | kPsrC);
}

TEST_F(CortexM3Test, ItEqNeMasksMatchArchitecturalItStateAdvanceForAllLengths) {
    // Independent ARM ITSTATE reference: CurrentCond is state[7:4]. Advance
    // clears at the final slot, otherwise shifts only state[4:0]. This covers
    // IT/ITT/ITE and all three-/four-slot variants for both condition parities.
    for (unsigned first_cond : {0u, 1u}) {
        for (unsigned mask = 1; mask <= 15; ++mask) {
            for (bool initial_z : {false, true}) {
                SCOPED_TRACE(::testing::Message()
                             << "cond=" << first_cond << " mask=" << mask << " Z=" << initial_z);
                std::vector<unsigned> reference;
                uint8_t state = static_cast<uint8_t>((first_cond << 4) | mask);
                while (state != 0) {
                    reference.push_back(state >> 4);
                    state = (state & 7u) == 0u
                                ? 0u
                                : static_cast<uint8_t>((state & 0xE0u) | ((state << 1) & 0x1Fu));
                }
                ASSERT_LE(reference.size(), 4u);
                const auto flags = (initial_z ? kPsrZ : 0u) | kPsrC | kPsrV;
                std::vector<uint16_t> program = {
                    0xF386,
                    0x8800, // MSR APSR_nzcvq, r6
                    static_cast<uint16_t>(0xBF00 | (first_cond << 4) | mask),
                };
                for (unsigned slot = 0; slot < reference.size(); ++slot) {
                    program.push_back(static_cast<uint16_t>(0x2011u + (slot << 8) + slot));
                }
                load_program(program);
                reset_cpu();
                set_reg(6, flags);
                start_cpu();
                ASSERT_TRUE(cpu_->step().has_value()); // MSR
                ASSERT_TRUE(cpu_->step().has_value()); // IT
                for (unsigned slot = 0; slot < reference.size(); ++slot) {
                    ASSERT_TRUE(cpu_->step().has_value());
                    const bool execute = reference[slot] == 0 ? initial_z : !initial_z;
                    EXPECT_EQ(reg(slot), execute ? 0x11u + slot : 0u);
                    EXPECT_EQ(cpu_->xpsr() & (kPsrN | kPsrZ | kPsrC | kPsrV), flags);
                }
            }
        }
    }
}

TEST_F(CortexM3Test, FinalItSlotSuppressesImplicitFlagsForThumb16DataProcessing) {
    struct Case {
        uint16_t opcode;
        uint32_t expected;
    };
    // GNU Thumb instruction encodings, with r0=5, r1=3, and carry-in one.
    const Case cases[] = {
        {0x2007, 7},          // MOVS r0, #7
        {0x0048, 6},          // LSLS r0, r1, #1
        {0x1840, 8},          // ADDS r0, r0, r1
        {0x3801, 4},          // SUBS r0, #1
        {0x4148, 9},          // ADCS r0, r1
        {0x4188, 2},          // SBCS r0, r1
        {0x4008, 1},          // ANDS r0, r1
        {0x4308, 7},          // ORRS r0, r1
        {0x4348, 15},         // MULS r0, r1
        {0x4248, 0xFFFFFFFD}, // RSBS r0, r1, #0
    };
    constexpr auto flags = kPsrZ | kPsrC | kPsrV;
    for (const auto& c : cases) {
        SCOPED_TRACE(c.opcode);
        load_program({0xF386, 0x8800, 0xBF08, c.opcode});
        reset_cpu();
        set_reg(0, 5);
        set_reg(1, 3);
        set_reg(6, flags);
        start_cpu();
        for (unsigned i = 0; i < 3; ++i) {
            ASSERT_TRUE(cpu_->step().has_value());
        }
        EXPECT_EQ(reg(0), c.expected);
        EXPECT_EQ(cpu_->xpsr() & (kPsrN | kPsrZ | kPsrC | kPsrV), flags);
        EXPECT_EQ(cpu_->pc().value(), 8u);
    }
}

TEST_F(CortexM3Test, ExplicitCompareStillUpdatesFlagsInFinalItSlot) {
    load_program({0xF386, 0x8800, 0xBF08, 0x2801}); // MSR; IT EQ; CMP r0,#1
    reset_cpu();
    set_reg(0, 0);
    set_reg(6, kPsrZ | kPsrC | kPsrV);
    start_cpu();
    for (unsigned i = 0; i < 3; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(cpu_->xpsr() & (kPsrN | kPsrZ | kPsrC | kPsrV), kPsrN);
}
