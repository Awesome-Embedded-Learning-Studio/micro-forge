#include <gtest/gtest.h>

#include "arch/toy/cpu.hpp"
#include "arch/toy/isa.hpp"
#include "memory/flat_memory.hpp"

using namespace micro_forge;
using namespace micro_forge::cpu;
using namespace micro_forge::cpu::toy;

// ── Test fixture ──

class ToyCpuTest : public ::testing::Test {
  protected:
    static constexpr addr_t kMemBase = 0;
    static constexpr addr_t kMemSize = 1024;

    memory::FlatMemory mem_{kMemSize};
    memory::Bus bus_;
    std::unique_ptr<ToyCPU> cpu_;

    void SetUp() override {
        ASSERT_TRUE(bus_.map(memory::region(kMemBase, kMemSize, mem_.GetWeak()))
                        .has_value());
        cpu_ = std::make_unique<ToyCPU>(bus_.GetWeak());
    }

    void load_program(const std::vector<data_t>& program, addr_t base = 0) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(program.data());
        ASSERT_TRUE(mem_.load(base, {bytes, program.size() * sizeof(data_t)})
                        .has_value());
    }

    void reset_cpu() { (void)cpu_->reset(); }
    void start_cpu() { cpu_->start(); }
    void step_cpu() { (void)cpu_->step(); }
    void set_reg(size_t idx, data_t val) {
        (void)cpu_->set_register_value(idx, val);
    }
    void set_pc(addr_t pc) { (void)cpu_->set_pc(pc); }

    data_t reg(size_t idx) {
        return cpu_->register_value(idx).value_or(0xDEAD);
    }

    void run_until_halt(size_t max_steps = 1000) {
        for (size_t i = 0; i < max_steps; ++i) {
            auto res = cpu_->step();
            if (!res.has_value()) {
                break;
            }
            auto st = cpu_->state();
            if (!st.has_value() || *st != CPU::State::Running) {
                break;
            }
        }
    }
};

// ── Basic lifecycle ──

TEST_F(ToyCpuTest, ResetClearsState) {
    reset_cpu();
    auto st = cpu_->state();
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, CPU::State::Halted);
    EXPECT_EQ(cpu_->pc().value_or(0xFFFF), 0u);
    EXPECT_EQ(cpu_->cycles().value_or(0xFFFF), 0u);
    for (size_t i = 0; i < RegCount; ++i) {
        EXPECT_EQ(reg(i), 0u) << "R" << i << " not zeroed";
    }
}

TEST_F(ToyCpuTest, SetPcAndRead) {
    set_pc(0x100);
    EXPECT_EQ(cpu_->pc().value_or(0), 0x100u);
}

TEST_F(ToyCpuTest, RegisterNames) {
    for (size_t i = 0; i < RegCount; ++i) {
        auto name = cpu_->register_name(i);
        ASSERT_TRUE(name.has_value());
        EXPECT_EQ(*name, std::string("R") + std::to_string(i));
    }
    auto bad = cpu_->register_name(RegCount);
    EXPECT_FALSE(bad.has_value());
}

TEST_F(ToyCpuTest, StepWhileHaltedReturnsError) {
    reset_cpu();
    auto res = cpu_->step();
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), CPU::CPUError::NotRunning);
}

TEST_F(ToyCpuTest, CyclesIncrement) {
    load_program({LDI::encode(0, 42), HALT::encode()});
    reset_cpu();
    start_cpu();
    step_cpu();
    EXPECT_EQ(cpu_->cycles().value_or(0), 1u);
    step_cpu();
    EXPECT_EQ(cpu_->cycles().value_or(0), 2u);
}

// ── ALU instructions ──

TEST_F(ToyCpuTest, LdiLoadsImmediate) {
    load_program({LDI::encode(0, 42), HALT::encode()});
    reset_cpu();
    start_cpu();
    run_until_halt();
    EXPECT_EQ(reg(0), 42u);
}

TEST_F(ToyCpuTest, AddPositive) {
    load_program({ADD::encode(2, 0, 1), HALT::encode()});
    reset_cpu();
    set_reg(0, 5);
    set_reg(1, 3);
    start_cpu();
    run_until_halt();
    EXPECT_EQ(reg(2), 8u);
}

TEST_F(ToyCpuTest, AddZeroSetsZFlag) {
    load_program({
        ADD::encode(2, 0, 1), // R2 = 0, Z=1
        BZ::encode(2),        // skip to HALT
        LDI::encode(3, 99),   // skipped
        HALT::encode(),
    });
    reset_cpu();
    set_reg(0, 0);
    set_reg(1, 0);
    start_cpu();
    run_until_halt();
    EXPECT_EQ(reg(3), 0u);
}

TEST_F(ToyCpuTest, SubBasic) {
    load_program({SUB::encode(2, 0, 1), HALT::encode()});
    reset_cpu();
    set_reg(0, 5);
    set_reg(1, 3);
    start_cpu();
    run_until_halt();
    EXPECT_EQ(reg(2), 2u);
}

TEST_F(ToyCpuTest, AndMask) {
    load_program({AND::encode(2, 0, 1), HALT::encode()});
    reset_cpu();
    set_reg(0, 0xFF);
    set_reg(1, 0x0F);
    start_cpu();
    run_until_halt();
    EXPECT_EQ(reg(2), 0x0Fu);
}

TEST_F(ToyCpuTest, NopDoesNothing) {
    load_program({NOP::encode(), LDI::encode(0, 7), HALT::encode()});
    reset_cpu();
    start_cpu();
    run_until_halt();
    EXPECT_EQ(reg(0), 7u);
    EXPECT_EQ(cpu_->cycles().value_or(0), 3u);
}

// ── Memory instructions ──

TEST_F(ToyCpuTest, StwAndLdw) {
    load_program({
        LDI::encode(0, 42),   // R0 = 42
        LDI::encode(1, 0),    // R1 = base addr = 0
        STW::encode(0, 1, 2), // mem[R1 + 2*4] = R0 → mem[8] = 42
        LDI::encode(0, 0),    // R0 = 0 (clobber)
        LDW::encode(0, 1, 2), // R0 = mem[R1 + 2*4] = 42
        HALT::encode(),
    });
    reset_cpu();
    start_cpu();
    run_until_halt();
    EXPECT_EQ(reg(0), 42u);
}

// ── Branch instructions ──

TEST_F(ToyCpuTest, BzTakenWhenZero) {
    load_program({
        ADD::encode(2, 0, 1), // R2=0, Z=1
        BZ::encode(2),        // skip LDI → land on HALT
        LDI::encode(3, 99),   // skipped
        NOP::encode(),        // padding
        HALT::encode(),
    });
    reset_cpu();
    set_reg(0, 0);
    set_reg(1, 0);
    start_cpu();
    run_until_halt();
    EXPECT_EQ(reg(3), 0u);
}

TEST_F(ToyCpuTest, BzNotTakenWhenNonZero) {
    load_program({
        ADD::encode(2, 0, 1), // R2=3, Z=0
        BZ::encode(2),        // not taken → fall through
        LDI::encode(3, 99),   // executed
        NOP::encode(),
        HALT::encode(),
    });
    reset_cpu();
    set_reg(0, 1);
    set_reg(1, 2);
    start_cpu();
    run_until_halt();
    EXPECT_EQ(reg(3), 99u);
}

TEST_F(ToyCpuTest, JmpUnconditional) {
    load_program({
        LDI::encode(0, 1),  // 0
        JMP::encode(3),     // 1: jump to instruction 3
        LDI::encode(0, 99), // 2: skipped
        HALT::encode(),     // 3
    });
    reset_cpu();
    start_cpu();
    run_until_halt();
    EXPECT_EQ(reg(0), 1u);
}

TEST_F(ToyCpuTest, BzBackward) {
    load_program({
        LDI::encode(1, 1),    // 0: R1 = 1
        LDI::encode(2, 3),    // 1: R2 = 3 (counter)
        SUB::encode(2, 2, 1), // 2: R2 -= 1
        BZ::encode(2),        // 3: if Z → jump to 6 (HALT)
        JMP::encode(2),       // 4: jump back to 2
        NOP::encode(),        // 5: padding
        HALT::encode(),       // 6
    });
    reset_cpu();
    start_cpu();
    run_until_halt();
    EXPECT_EQ(reg(2), 0u);
}

// ── CALL / RET ──

TEST_F(ToyCpuTest, CallRet) {
    load_program({
        LDI::encode(0, 10), // 0: R0 = 10
        CALL::encode(4),    // 1: call func at 4
        LDI::encode(0, 99), // 2: R0 = 99 (after return)
        HALT::encode(),     // 3
        // func:
        LDI::encode(1, 20),   // 4: R1 = 20
        ADD::encode(0, 0, 1), // 5: R0 += 20 → 30
        RET::encode(),        // 6
    });
    reset_cpu();
    set_reg(7, 512);
    start_cpu();
    run_until_halt();
    EXPECT_EQ(reg(0), 99u);
}

TEST_F(ToyCpuTest, NestedCallRet) {
    load_program({
        LDI::encode(0, 1), // 0
        CALL::encode(4),   // 1: call A
        HALT::encode(),    // 2
        NOP::encode(),     // 3
        // A:
        LDI::encode(1, 10),   // 4
        CALL::encode(8),      // 5: call B
        ADD::encode(0, 0, 1), // 6: R0 += R1
        RET::encode(),        // 7
        // B:
        LDI::encode(2, 5),    // 8
        ADD::encode(1, 1, 2), // 9: R1 += R2
        RET::encode(),        // 10
    });
    reset_cpu();
    set_reg(7, 512);
    start_cpu();
    run_until_halt();
    // R0=1, R1=10→15(after B), then A: R0=1+15=16
    EXPECT_EQ(reg(0), 16u);
    EXPECT_EQ(reg(1), 15u);
}

// ── HALT ──

TEST_F(ToyCpuTest, HaltStopsExecution) {
    load_program({
        LDI::encode(0, 42),
        HALT::encode(),
        LDI::encode(0, 99),
    });
    reset_cpu();
    start_cpu();
    run_until_halt();
    EXPECT_EQ(reg(0), 42u);
    auto st = cpu_->state();
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, CPU::State::Halted);
}

// ── INT / raise_irq ──

TEST_F(ToyCpuTest, IntTriggersHandlerAndReturns) {
    load_program({
        LDI::encode(0, 1),  // 0: R0 = 1
        INT::encode(3),     // 1: queue IRQ 3
        LDI::encode(0, 99), // 2: executed before handler
        HALT::encode(),     // 3
        NOP::encode(),      // 4: padding
        // handler at 5:
        LDI::encode(1, 77), // 5: R1 = 77
        RET::encode(),      // 6
    });
    reset_cpu();
    set_reg(7, 512);
    cpu_->set_interrupt_vector(3, 5 * 4);
    start_cpu();
    run_until_halt(20);
    EXPECT_EQ(reg(0), 99u);
    EXPECT_EQ(reg(1), 77u);
}

TEST_F(ToyCpuTest, RaiseIrqExternalJumpsToHandler) {
    load_program({
        LDI::encode(0, 10), // 0: R0 = 10
        LDI::encode(0, 20), // 1: R0 = 20
        HALT::encode(),     // 2
        NOP::encode(),      // 3
        // handler at 4:
        LDI::encode(1, 55), // 4
        RET::encode(),      // 5
    });
    reset_cpu();
    set_reg(7, 512);
    cpu_->set_interrupt_vector(5, 4 * 4);
    start_cpu();
    step_cpu(); // 0: LDI R0,10
    EXPECT_EQ(reg(0), 10u);
    (void)cpu_->raise_irq(5);
    step_cpu(); // poll_intr → handler: LDI R1,55
    EXPECT_EQ(reg(1), 55u);
    step_cpu(); // RET
    step_cpu(); // 1: LDI R0,20
    EXPECT_EQ(reg(0), 20u);
    step_cpu(); // 2: HALT
    auto st = cpu_->state();
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, CPU::State::Halted);
}

// ── Error handling ──

TEST_F(ToyCpuTest, FetchUnmappedFaults) {
    reset_cpu();
    start_cpu();
    set_pc(2000);
    auto res = cpu_->step();
    EXPECT_FALSE(res.has_value());
    auto st = cpu_->state();
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, CPU::State::Faulted);
}

TEST_F(ToyCpuTest, UnknownOpcodeFaults) {
    data_t bad_insn = (0xDu << 28);
    load_program({bad_insn});
    reset_cpu();
    start_cpu();
    auto res = cpu_->step();
    EXPECT_FALSE(res.has_value());
    auto st = cpu_->state();
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, CPU::State::Faulted);
}

// ── Integration: Fibonacci ──

TEST_F(ToyCpuTest, Fibonacci10) {
    load_program({
        LDI::encode(0, 0),    // 0: R0 = 0
        LDI::encode(1, 1),    // 1: R1 = 1
        LDI::encode(2, 10),   // 2: R2 = 10
        LDI::encode(3, 1),    // 3: R3 = 1
        ADD::encode(0, 0, 1), // 4: R0 += R1
        SUB::encode(4, 0, 1), // 5: R4 = R0 - R1
        AND::encode(1, 4, 4), // 6: R1 = R4
        SUB::encode(2, 2, 3), // 7: R2 -= 1
        BZ::encode(2),        // 8: if Z → 10
        JMP::encode(4),       // 9: loop
        HALT::encode(),       // 10
    });
    reset_cpu();
    start_cpu();
    run_until_halt();
    EXPECT_EQ(reg(0), 55u);
}

// ── Register count ──

TEST_F(ToyCpuTest, RegisterCount) {
    auto cnt = cpu_->register_count();
    ASSERT_TRUE(cnt.has_value());
    EXPECT_EQ(*cnt, RegCount);
}
