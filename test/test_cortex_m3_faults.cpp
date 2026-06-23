#include "test_cortex_m3_common.hpp"

// ── Fetch unmapped fault ──

TEST_F(CortexM3Test, FetchUnmappedFaults) {
    reset_cpu();
    start_cpu();
    set_pc(5000); // Outside 4096-byte memory
    auto res = cpu_->step();
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), CPU::CPUError::InstructionFetchFault);
    auto st = cpu_->state();
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, CPU::State::Faulted);
}

// ── Illegal instruction fault ──

TEST_F(CortexM3Test, IllegalInstructionFaults) {
    // 0b01001_xxxx_xxxx_xxxx with op=0b11 is BX
    // Use a genuinely unassigned encoding: 0b11011_xxxx (not handled by
    // decoder)
    load_program({0xDE00}); // decode_key = 0b11011 — unimplemented
    reset_cpu();
    start_cpu();
    auto res = cpu_->step();
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), CPU::CPUError::IllegalInstruction);
    auto st = cpu_->state();
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, CPU::State::Faulted);
}

// ── FaultRecord ──

TEST_F(CortexM3Test, FetchUnmappedRecordsFault) {
    reset_cpu();
    start_cpu();
    set_pc(5000);
    auto res = cpu_->step();
    ASSERT_FALSE(res.has_value());
    auto& fr = cpu_->last_fault();
    ASSERT_TRUE(fr.has_value());
    EXPECT_EQ(fr->pc, 5000u);
    EXPECT_EQ(fr->kind, CPU::CPUError::InstructionFetchFault);
}

TEST_F(CortexM3Test, IllegalInstructionRecordsFault) {
    load_program({0xDE00});
    reset_cpu();
    start_cpu();
    auto res = cpu_->step();
    ASSERT_FALSE(res.has_value());
    auto& fr = cpu_->last_fault();
    ASSERT_TRUE(fr.has_value());
    EXPECT_EQ(fr->opcode16, 0xDE00u);
    EXPECT_EQ(fr->kind, CPU::CPUError::IllegalInstruction);
    EXPECT_FALSE(fr->is_32bit);
}

TEST_F(CortexM3Test, DataAccessFaultRecordsBusContext) {
    load_program({0x6848}); // ldr r0, [r1, #4]
    reset_cpu();
    set_reg(1, 0x1000);
    start_cpu();

    auto res = cpu_->step();
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), CPU::CPUError::DataAccessFault);

    auto& fr = cpu_->last_fault();
    ASSERT_TRUE(fr.has_value());
    ASSERT_TRUE(fr->bus_error.has_value());
    ASSERT_TRUE(fr->access_addr.has_value());
    ASSERT_TRUE(fr->access_width.has_value());
    EXPECT_EQ(*fr->bus_error, BusError::Unmapped);
    EXPECT_EQ(*fr->access_addr, 0x1004u);
    EXPECT_EQ(*fr->access_width, Width::Word);
}

TEST_F(CortexM3Test, IllegalInstructionEmitsFaultLog) {
    std::vector<std::string> messages;
    micro_forge::util::set_log_sink([&](micro_forge::util::LogLevel level,
                                        std::string_view module,
                                        std::string_view message) {
        if (level == micro_forge::util::LogLevel::Error && module == "fault") {
            messages.emplace_back(message);
        }
    });

    load_program({0xDE00});
    reset_cpu();
    start_cpu();
    auto res = cpu_->step();
    micro_forge::util::reset_log_sink();

    ASSERT_FALSE(res.has_value());
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_NE(messages[0].find("PC=0x00000000"), std::string::npos);
    EXPECT_NE(messages[0].find("opcode=0xDE00"), std::string::npos);
    EXPECT_NE(messages[0].find("kind=IllegalInstruction"), std::string::npos);
}

// ── Register count ──

TEST_F(CortexM3Test, RegisterCount) {
    auto cnt = cpu_->register_count();
    ASSERT_TRUE(cnt.has_value());
    EXPECT_EQ(*cnt, 16u);
}

TEST_F(CortexM3Test, CpsAndBarrierInstructions) {
    load_program({
        0xB672, // cpsid i
        0xF3EF,
        0x8010, // mrs r0, primask
        0xF3BF,
        0x8F5F, // dmb sy
        0xF3BF,
        0x8F4F, // dsb sy
        0xF3BF,
        0x8F6F, // isb sy
        0xB662, // cpsie i
        0xF3EF,
        0x8110, // mrs r1, primask
    });
    reset_cpu();
    start_cpu();
    for (int i = 0; i < 7; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(0), 1u);
    EXPECT_EQ(reg(1), 0u);
}

TEST_F(CortexM3Test, SvcEntersException11AndReturns) {
    ASSERT_TRUE(bus_.map(memory::region(0x08000000, kMemSize, mem_.GetWeak()))
                    .has_value());
    ASSERT_TRUE(mem_.write(11 * 4, 0x00000101u, Width::Word).has_value());
    load_program({0xDF12, 0x2001}, 0x80);  // svc #0x12; movs r0, #1
    load_program({0x2007, 0x4770}, 0x100); // movs r0, #7; bx lr

    reset_cpu();
    set_pc(0x08000080);
    set_reg(13, 0x300);
    start_cpu();

    ASSERT_TRUE(cpu_->step().has_value()); // SVC entry
    EXPECT_TRUE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->pc().value_or(0), 0x100u);

    ASSERT_TRUE(cpu_->step().has_value()); // handler body
    EXPECT_EQ(reg(0), 7u);
    ASSERT_TRUE(cpu_->step().has_value()); // exception return
    EXPECT_FALSE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->pc().value_or(0), 0x08000082u);

    ASSERT_TRUE(cpu_->step().has_value()); // instruction after SVC
    EXPECT_EQ(reg(0), 1u);
}

// ── ARMv7E-M DSP (Cortex-M3 scope-out): must clean-fault, not silently decode ──
// Cortex-M3 = ARMv7-M base (no DSP extension). These instructions are
// UndefinedInstruction on M3. Encodings sourced authoritatively from
// `arm-none-eabi-as -mcpu=cortex-m4` + objdump (the M3 assembler rejects them).
// Guards the dispatch table against silent mis-decode: several land inside an
// existing handler's mask (PKHBT/PKHTB in dataproc-reg 0xEA00; SXTAH/UXTB16 in
// shift-reg 0xFA00) and must be carved out so they fault, not execute.
TEST_F(CortexM3Test, V7emDspInstructionsCleanFault) {
    struct Case {
        uint16_t hw1, hw2;
        const char* name;
    };
    const Case cases[] = {
        {0xFA82, 0xF081, "qadd"},     {0xFA82, 0xF0A1, "qsub"},
        {0xFA82, 0xF091, "qdadd"},    {0xFA82, 0xF0B1, "qdsub"},
        {0xEAC1, 0x1002, "pkhbt"},    {0xEAC1, 0x1022, "pkhtb"},
        {0xFAA1, 0xF082, "sel"},
        {0xFA01, 0xF082, "sxtah"},    {0xFA11, 0xF082, "uxtah"},
        {0xFA2F, 0xF081, "sxtb16"},   {0xFA3F, 0xF081, "uxtb16"},
        {0xFBE2, 0x0163, "umaal"},
        {0xFB21, 0x3002, "smlad"},    {0xFB41, 0x3002, "smlsd"},
        {0xFBC2, 0x01C3, "smlald"},   {0xFBC2, 0x01D3, "smlaldx"},
        {0xFB71, 0xF002, "usad8"},    {0xFB71, 0x3002, "usada8"},
    };
    for (const auto& c : cases) {
        load_program({c.hw1, c.hw2});
        reset_cpu();
        start_cpu();
        auto res = cpu_->step();
        EXPECT_FALSE(res.has_value()) << c.name << " should fault";
        if (!res.has_value()) {
            EXPECT_EQ(res.error(), CPU::CPUError::IllegalInstruction)
                << c.name;
        }
        auto st = cpu_->state();
        ASSERT_TRUE(st.has_value()) << c.name;
        EXPECT_EQ(*st, CPU::State::Faulted) << c.name;
    }
}

TEST_F(CortexM3Test, MrsMsrExtendedSystemRegisters) {
    load_program({
        0xF380, 0x8810, // msr primask, r0
        0xF381, 0x8811, // msr basepri, r1
        0xF382, 0x8813, // msr faultmask, r2
        0xF383, 0x8814, // msr control, r3
        0xF384, 0x8808, // msr msp, r4
        0xF385, 0x8809, // msr psp, r5
        0xF386, 0x8800, // msr apsr_nzcvq, r6
        0xF3EF, 0x8010, // mrs r0, primask
        0xF3EF, 0x8111, // mrs r1, basepri
        0xF3EF, 0x8213, // mrs r2, faultmask
        0xF3EF, 0x8314, // mrs r3, control
        0xF3EF, 0x8408, // mrs r4, msp
        0xF3EF, 0x8509, // mrs r5, psp
        0xF3EF, 0x8600, // mrs r6, apsr
    });
    reset_cpu();
    set_reg(0, 1);
    set_reg(1, 0x40);
    set_reg(2, 1);
    set_reg(3, 2);
    set_reg(4, 0x200);
    set_reg(5, 0x240);
    set_reg(6, PSR_N | PSR_C);
    start_cpu();

    for (int i = 0; i < 14; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(0), 1u);
    EXPECT_EQ(reg(1), 0x40u);
    EXPECT_EQ(reg(2), 1u);
    EXPECT_EQ(reg(3), 2u);
    EXPECT_EQ(reg(4), 0x200u);
    EXPECT_EQ(reg(5), 0x240u);
    EXPECT_EQ(reg(6) & (PSR_N | PSR_Z | PSR_C | PSR_V), PSR_N | PSR_C);
    EXPECT_EQ(reg(13), 0x240u);
}
