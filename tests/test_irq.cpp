/// @file test_irq.cpp
/// @brief Batch-4 acceptance: a stub device raises a real interrupt line
///        at a fixed icount and the CPU observes it deterministically.

#include <gtest/gtest.h>

#include <micro_forge/arch/x86_64/cpu.hpp>
#include <micro_forge/kernel/bus/bus.hpp>
#include <micro_forge/kernel/bus/irq_line.hpp>
#include <micro_forge/kernel/memory/ram_region.hpp>
#include <micro_forge/kernel/scheduler.hpp>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace {

using micro_forge::arch::x86_64::Cpu;
using micro_forge::arch::x86_64::ExitStatus;
using micro_forge::kernel::Bus;
using micro_forge::kernel::IrqLine;
using micro_forge::kernel::RamRegion;
using micro_forge::kernel::Scheduler;
using micro_forge::kernel::Tick;

constexpr uint64_t kCodeBase = 0x100000;
constexpr uint64_t kStackTop = 0x90000;

/// One run of a short loop program with a line raised at @p raise_at.
/// The CPU hook acknowledges (lowers) the line, so each raise is
/// observed exactly once.
struct IrqRun {
    std::vector<Tick> observed;
    ExitStatus exit{};
};

IrqRun run_with_irq(Tick raise_at) {
    Bus bus;
    RamRegion ram(0x1000000);
    Scheduler scheduler;
    Cpu cpu(bus, scheduler);
    if (!bus.map(0, ram.size(), ram).has_value()) {
        ADD_FAILURE() << "mapping RAM failed";
        return {};
    }
    // mov ecx, 10; dec rcx; jnz -5 (a 10-iteration loop), then hlt.
    const std::initializer_list<uint8_t> code = {
        0xB9, 0x0A, 0x00, 0x00, 0x00, // mov ecx, 10
        0x48, 0xFF, 0xC9,             // dec rcx
        0x75, 0xFB,
    }; // jnz dec
    std::ranges::copy(code, ram.data() + kCodeBase);
    ram.data()[kCodeBase + code.size()] = 0xF4; // hlt terminator
    cpu.reset(kCodeBase, kStackTop);

    IrqLine line;
    IrqRun run;
    cpu.attach_irq(line, [&] {
        run.observed.push_back(cpu.icount());
        line.lower(); // the receiver acknowledges the level line
    });
    scheduler.schedule(raise_at, [&line] { line.raise(); });
    run.exit = cpu.run(1000);
    return run;
}

TEST(IrqLineTest, StubDeviceReachesCpuAtDeterministicIcount) {
    const IrqRun first = run_with_irq(7);
    ASSERT_EQ(ExitStatus::Kind::kHalt, first.exit.kind);
    ASSERT_EQ(1U, first.observed.size());
    EXPECT_EQ(7U, first.observed[0]); // observed in the same retire

    const IrqRun second = run_with_irq(7);
    EXPECT_EQ(first.observed, second.observed);
}

TEST(IrqLineTest, LineObservationFollowsRaisePoint) {
    // The program retires 22 instructions; both raise points are inside.
    const IrqRun early = run_with_irq(2);
    const IrqRun late = run_with_irq(21);
    ASSERT_EQ(1U, early.observed.size());
    ASSERT_EQ(1U, late.observed.size());
    EXPECT_EQ(2U, early.observed[0]);
    EXPECT_EQ(21U, late.observed[0]);
}

TEST(IrqLineTest, UnacknowledgedLineFiresEveryFollowingInstruction) {
    Bus bus;
    RamRegion ram(0x1000000);
    Scheduler scheduler;
    Cpu cpu(bus, scheduler);
    ASSERT_TRUE(bus.map(0, ram.size(), ram).has_value());
    const std::initializer_list<uint8_t> code = {
        0xB9, 0x0A, 0x00, 0x00, 0x00, 0x48, 0xFF, 0xC9, 0x75, 0xFB,
    };
    std::ranges::copy(code, ram.data() + kCodeBase);
    ram.data()[kCodeBase + code.size()] = 0xF4;
    cpu.reset(kCodeBase, kStackTop);

    IrqLine line;
    std::vector<Tick> observed;
    cpu.attach_irq(line, [&] { observed.push_back(cpu.icount()); }); // no ack
    scheduler.schedule(3, [&line] { line.raise(); });
    (void)cpu.run(1000);
    // Level semantics: from the raise on, every retire observes the line.
    // The program retires 22 instructions total (setup 1 + loop 20 + hlt),
    // so icount 3..22 is 20 observations, ending at the halt itself.
    ASSERT_EQ(20U, observed.size());
    EXPECT_EQ(3U, observed[0]);
    EXPECT_EQ(22U, observed[19]);
}

} // namespace
