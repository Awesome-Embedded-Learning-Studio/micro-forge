/// @file test_execute.cpp
/// @brief Batch-3 acceptance: the bare client runs end to end on the
///        executor (exit code 170 = success path through ALU, branches,
///        memory, push/pop, call/ret), and two identical runs produce a
///        bit-identical instruction trace (determinism contract).

#include <gtest/gtest.h>

#include <micro_forge/arch/x86_64/cpu.hpp>
#include <micro_forge/kernel/bus.hpp>
#include <micro_forge/kernel/ram_region.hpp>
#include <micro_forge/kernel/scheduler.hpp>

#include <cstdint>
#include <fstream>
#include <functional>
#include <tuple>
#include <vector>

namespace {

using micro_forge::arch::x86_64::Cpu;
using micro_forge::arch::x86_64::ExitStatus;
using micro_forge::kernel::Bus;
using micro_forge::kernel::RamRegion;
using micro_forge::kernel::Scheduler;
using micro_forge::kernel::Tick;

constexpr uint64_t kCodeBase = 0x100000;
constexpr uint64_t kStackTop = 0x90000;
constexpr uint64_t kExitSuccessPath = 0xAA; // mov rdi, 0xAA on the pass path

struct RunTrace {
    ExitStatus exit{};
    Tick icount = 0;
    uint64_t rip_hash = 0; // FNV-1a over every retired rip
    uint64_t event_fired = 0;
    std::vector<char> console;
};

/// One full run of the bare client under a fresh machine: same image,
/// same entry, same scheduled event. Any divergence between two runs is
/// a determinism break by construction.
RunTrace run_bare_client(const char* image_path, bool with_event) {
    std::ifstream file(image_path, std::ios::binary);
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());

    Bus bus;
    RamRegion ram(0x1000000);
    Scheduler scheduler;
    Cpu cpu(bus, scheduler);
    RunTrace trace;
    if (image.empty()) {
        ADD_FAILURE() << "bare client image missing: " << image_path;
        return trace;
    }
    if (!bus.map(0, ram.size(), ram).has_value()) {
        ADD_FAILURE() << "mapping RAM failed";
        return trace;
    }
    std::ranges::copy(image, ram.data() + kCodeBase);
    cpu.reset(kCodeBase, kStackTop);

    uint64_t hash = 0xCBF29CE484222325ULL; // FNV-1b offset basis
    cpu.set_trace([&hash](uint64_t rip) {
        hash ^= rip;
        hash *= 0x100000001B3ULL;
    });
    const Cpu* cpu_view = &cpu;
    if (with_event) {
        scheduler.schedule(20, [cpu_view, &trace] { trace.event_fired = cpu_view->icount(); });
    }

    trace.exit = cpu.run(100000);
    trace.icount = cpu.icount();
    trace.rip_hash = hash;
    trace.console = cpu.console();
    return trace;
}

TEST(BareClientExecuteTest, RunsEndToEndWithSuccessExitCode) {
    const RunTrace trace = run_bare_client(BARE_CLIENT_BIN, true);
    EXPECT_EQ(ExitStatus::Kind::kExit, trace.exit.kind);
    EXPECT_EQ(kExitSuccessPath, trace.exit.code);
    EXPECT_TRUE(trace.console.empty());
    EXPECT_EQ(20U, trace.event_fired); // event fires exactly on icount 20
    EXPECT_GT(trace.icount, 40U);      // loop plus memory plus call/ret path
}

TEST(BareClientExecuteTest, IdenticalRunsProduceIdenticalTraces) {
    const RunTrace first = run_bare_client(BARE_CLIENT_BIN, true);
    const RunTrace second = run_bare_client(BARE_CLIENT_BIN, true);
    EXPECT_EQ(first.exit.kind, second.exit.kind);
    EXPECT_EQ(first.exit.code, second.exit.code);
    EXPECT_EQ(first.icount, second.icount);
    EXPECT_EQ(first.rip_hash, second.rip_hash);
    EXPECT_EQ(first.event_fired, second.event_fired);
    EXPECT_EQ(first.console, second.console);
}

} // namespace
