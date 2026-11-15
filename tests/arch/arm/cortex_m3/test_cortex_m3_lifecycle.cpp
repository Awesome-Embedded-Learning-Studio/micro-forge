#include "test_cortex_m3_common.hpp"

// ── Lifecycle ──

TEST_F(CortexM3Test, ResetClearsState) {
    reset_cpu();
    auto st = cpu_->state();
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, CPU::State::kHalted);
    EXPECT_EQ(cpu_->cycles().value_or(0xFFFF), 0u);
    for (size_t i = 0; i < 16; ++i) {
        EXPECT_EQ(reg(i), 0u) << "R" << i << " not zeroed";
    }
}

TEST_F(CortexM3Test, SetPcAndRead) {
    reset_cpu();
    set_pc(0x100);
    EXPECT_EQ(cpu_->pc().value_or(0), 0x100u);
}

TEST_F(CortexM3Test, RegisterNames) {
    reset_cpu();
    for (size_t i = 0; i < 16; ++i) {
        auto name = cpu_->register_name(i);
        ASSERT_TRUE(name.has_value());
    }
    auto bad = cpu_->register_name(16);
    EXPECT_FALSE(bad.has_value());
}

TEST_F(CortexM3Test, StepWhileHaltedReturnsError) {
    reset_cpu();
    auto res = cpu_->step();
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), CPU::CPUError::kNotRunning);
}

TEST_F(CortexM3Test, SpLowBitsMasked) {
    reset_cpu();
    set_reg(13, 0x20000003);
    EXPECT_EQ(reg(13), 0x20000000u);
}

TEST_F(CortexM3Test, ResetWakesSleepingCpuAndExecutesReplacementProgram) {
    load_program({0xBF30}); // WFI
    reset_cpu();
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    ASSERT_TRUE(cpu_->is_sleeping());

    reset_cpu();
    EXPECT_FALSE(cpu_->is_sleeping());
    load_program({0x202A}); // MOVS r0, #42
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 42u);
    EXPECT_EQ(cpu_->pc().value_or(0), 2u);
}

TEST_F(CortexM3Test, ResetClearsFaultAndExceptionEntryState) {
    load_program({0xDE00});
    reset_cpu();
    start_cpu();
    ASSERT_FALSE(cpu_->step().has_value());
    ASSERT_TRUE(cpu_->last_fault().has_value());

    reset_cpu();
    EXPECT_FALSE(cpu_->last_fault().has_value());
    EXPECT_FALSE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->state().value(), CPU::State::kHalted);
    EXPECT_EQ(cpu_->xpsr(), kPsrT);
    EXPECT_EQ(cpu_->primask(), 0u);
    EXPECT_EQ(cpu_->basepri(), 0u);
    EXPECT_EQ(cpu_->faultmask(), 0u);
    EXPECT_EQ(cpu_->control(), 0u);
    EXPECT_EQ(cpu_->msp(), 0u);
    EXPECT_EQ(cpu_->psp(), 0u);
    EXPECT_EQ(cpu_->cycles().value(), 0u);

    // A later fault must describe the new access, not the previous instruction.
    load_program({0x6848}); // LDR r0, [r1, #4]
    set_reg(1, kMemSize);
    start_cpu();
    ASSERT_FALSE(cpu_->step().has_value());
    ASSERT_TRUE(cpu_->last_fault().has_value());
    EXPECT_EQ(cpu_->last_fault()->kind, CPU::CPUError::kDataAccessFault);
    EXPECT_EQ(cpu_->last_fault()->access_addr, kMemSize + 4);
}

TEST_F(CortexM3Test, ResetDiscardsUnfinishedItBlock) {
    load_program({0xBF08}); // IT EQ, with Z clear after reset
    reset_cpu();
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());

    reset_cpu();
    load_program({0x2107});
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(1), 7u);
}

TEST_F(CortexM3Test, ResetClearsProbeObservations) {
    load_program({0xDE00});
    reset_cpu();
    cpu_->enable_probe_mode();
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    ASSERT_EQ(cpu_->missing_opcodes().size(), 1u);
    reset_cpu();
    EXPECT_TRUE(cpu_->missing_opcodes().empty());
}

TEST_F(CortexM3Test, VectorTableResetInitializesRegistersThroughSharedBus) {
    constexpr addr_t vector_base = 0x200;
    ASSERT_TRUE(bus_.write_u32(vector_base, 0x800).has_value());
    ASSERT_TRUE(bus_.write_u32(vector_base + 4, 0x101).has_value());
    load_program({0x2037}, 0x100);
    set_reg(0, 99);

    ASSERT_TRUE(cortex_m3_reset(*cpu_, access_, vector_base).has_value());
    EXPECT_EQ(cpu_->state().value(), CPU::State::kHalted);
    EXPECT_EQ(reg(13), 0x800u);
    EXPECT_EQ(cpu_->msp(), 0x800u);
    EXPECT_EQ(reg(14), 0xFFFFFFFFu);
    EXPECT_EQ(cpu_->pc().value(), 0x100u);
    EXPECT_EQ(cpu_->xpsr(), kPsrT);
    EXPECT_EQ(reg(0), 0u);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 55u);
}

TEST_F(CortexM3Test, VectorTableResetRejectsUnreadableWords) {
    EXPECT_FALSE(cortex_m3_reset(*cpu_, access_, kMemSize).has_value());
    EXPECT_FALSE(cortex_m3_reset(*cpu_, access_, kMemSize - 4).has_value());
}

TEST_F(CortexM3Test, ResetClearsSpecialRegistersAndPendingSysTick) {
    load_program({
        0xF380,
        0x8810, // MSR PRIMASK, r0
        0xF381,
        0x8811, // MSR BASEPRI, r1
        0xF382,
        0x8813, // MSR FAULTMASK, r2
        0xF383,
        0x8814, // MSR CONTROL, r3
        0xF384,
        0x8808, // MSR MSP, r4
        0xF385,
        0x8809, // MSR PSP, r5
    });
    reset_cpu();
    set_reg(0, 1);
    set_reg(1, 0x40);
    set_reg(2, 1);
    set_reg(3, 2);
    set_reg(4, 0x200);
    set_reg(5, 0x240);
    start_cpu();
    for (int i = 0; i < 6; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    ASSERT_EQ(cpu_->primask(), 1u);
    ASSERT_EQ(cpu_->basepri(), 0x40u);
    ASSERT_EQ(cpu_->faultmask(), 1u);
    ASSERT_EQ(cpu_->control(), 2u);
    ASSERT_EQ(cpu_->msp(), 0x200u);
    ASSERT_EQ(cpu_->psp(), 0x240u);
    cpu_->sys_tick_irq();

    reset_cpu();
    EXPECT_EQ(cpu_->primask(), 0u);
    EXPECT_EQ(cpu_->basepri(), 0u);
    EXPECT_EQ(cpu_->faultmask(), 0u);
    EXPECT_EQ(cpu_->control(), 0u);
    EXPECT_EQ(cpu_->msp(), 0u);
    EXPECT_EQ(cpu_->psp(), 0u);
    load_program({0x2001});
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 1u);
    EXPECT_FALSE(cpu_->in_handler_mode());
}

TEST_F(CortexM3Test, DecoderCacheStillFetchesThroughSharedBusAndPropagatesErrors) {
    class FaultingFetchRam final : public kernel::RamRegion {
    public:
        FaultingFetchRam() : RamRegion(16) {}
        bool reject_fetch = false;
        unsigned fetches = 0;
        std::expected<uint16_t, kernel::BusError> read_u16(uint64_t offset) override {
            ++fetches;
            if (reject_fetch) {
                return std::unexpected{
                    kernel::BusError{kernel::BusError::Kind::kDeviceFault, offset}};
            }
            return RamRegion::read_u16(offset);
        }
    } code;
    constexpr addr_t code_base = 0x10000;
    ASSERT_TRUE(bus_.map(code_base, 16, code).has_value());
    ASSERT_TRUE(bus_.write_u16(code_base, 0x2001).has_value());
    reset_cpu();
    cpu_->set_jit_enabled(true);
    set_pc(code_base);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    ASSERT_EQ(code.fetches, 1u);
    set_pc(code_base);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(code.fetches, 2u);

    code.reject_fetch = true;
    set_pc(code_base);
    const auto result = cpu_->step();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CPU::CPUError::kInstructionFetchFault);
    EXPECT_EQ(code.fetches, 3u);
    ASSERT_TRUE(cpu_->last_fault().has_value());
    EXPECT_EQ(cpu_->last_fault()->bus_error, device::BusError::kPeripheralFault);
    EXPECT_EQ(cpu_->last_fault()->access_addr, code_base);
    EXPECT_EQ(cpu_->last_fault()->access_width, Width::kHalfWord);
}

TEST_F(CortexM3Test, DecoderCacheIsDisabledByDefault) {
    EXPECT_FALSE(cpu_->jit_enabled());
}

TEST_F(CortexM3Test, DecoderCacheObservesAliasWritesAndReset) {
    ASSERT_TRUE(bus_.map(0x20000000, kMemSize, mem_).has_value());
    load_program({0x2001});
    reset_cpu();
    cpu_->set_jit_enabled(true);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 1u);

    ASSERT_TRUE(bus_.write_u16(0x20000000, 0x2002).has_value());
    set_pc(0);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 2u);

    reset_cpu();
    ASSERT_TRUE(bus_.write_u16(0, 0x2003).has_value());
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 3u);
}

TEST_F(CortexM3Test, DecoderCacheObservesDirectBackingChangesAndInstructionLength) {
    load_program({0x2001});
    reset_cpu();
    cpu_->set_jit_enabled(true);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());

    // Loader writes bypass Bus write notification. Replace 16-bit MOVS with
    // 32-bit MOVW, then change only its second halfword, then return to MOVS.
    const uint8_t movw[] = {0x40, 0xF2, 0x02, 0x00};
    ASSERT_TRUE(mem_.copy_in(0, movw));
    set_pc(0);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 2u);
    EXPECT_EQ(cpu_->pc().value(), 4u);

    ASSERT_TRUE(bus_.write_u16(2, 0x0003).has_value());
    set_pc(0);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 3u);

    ASSERT_TRUE(bus_.write_u16(0, 0x2004).has_value());
    set_pc(0);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 4u);
    EXPECT_EQ(cpu_->pc().value(), 2u);
}

TEST_F(CortexM3Test, DecoderCacheDoesNotHideReplacementIllegalInstruction) {
    load_program({0x2001});
    reset_cpu();
    cpu_->set_jit_enabled(true);
    start_cpu();
    ASSERT_TRUE(cpu_->step().has_value());

    ASSERT_TRUE(bus_.write_u16(0, 0xDE00).has_value());
    set_pc(0);
    const auto result = cpu_->step();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CPU::CPUError::kIllegalInstruction);
    ASSERT_TRUE(cpu_->last_fault().has_value());
    EXPECT_EQ(cpu_->last_fault()->opcode16, 0xDE00);
}
