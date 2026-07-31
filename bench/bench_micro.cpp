// bench_micro.cpp — Google Benchmark micro-benchmarks for micro-forge.
//
// Companion to bench_sim.cpp: where bench_sim measures end-to-end SoC
// throughput (insn/s over a real armcc/AC6 corpus) with hand-rolled chrono,
// this file ISOLATES each hot path with Google Benchmark to answer "WHERE
// do the per-cycle nanoseconds actually go" — the prerequisite for a
// translation-cache/JIT design that targets the right cost:
//
//   BM_DispatchBranchSelf   one `B .` step — floor of fetch+decode+dispatch+
//                           pc-write, zero data-bus traffic. (dispatch headroom)
//   BM_BusReadFlat          one Bus::read vs a 1-region FlatMemory — the Region
//                           find + WeakPtr IsValid + Device virtual + byte loop.
//   BM_SysTickTick          one SysTick::tick(1) — the cost the coordinator
//                           pays every step (P1 event-driven rewrite target).
//   BM_BusReadMultiRegion   Bus::read vs the LAST of N regions — find_region's
//                           linear scan + per-region WeakPtr IsValid scaling.
//
// NOTE: the bench build defines MF_PERF_STATS PUBLIC, so find_region's
// per-call counter bookkeeping (record_find_region) is active here — real
// production builds (stats OFF) pay strictly less than these numbers.
//
// Built under MICRO_FORGE_BUILD_BENCH=ON (RelWithDebInfo in build-rel/).

#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "memory/bus.hpp"
#include "memory/flat_memory.hpp"
#include "periph/nvic.hpp"
#include "periph/systick.hpp"
#include "sim/coordinator.hpp"
#include "sim/virtual_clock.hpp"
#include "util/perf_stats.hpp"

#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

using namespace micro_forge;
using namespace micro_forge::memory;
using namespace micro_forge::cpu::arm::cortex_m3;

namespace {

// Bare core + flat memory, mirroring the test_cortex_m3_common fixture but
// without gtest. A 4 KiB flat region at 0x0 so fetches never touch a real
// peripheral — this is what isolates "dispatch" from "SoC".
struct BareCore {
    FlatMemory mem{4096};
    Bus bus;
    CortexM3CPU cpu{&bus};

    BareCore() {
        (void)bus.map(region(0, 4096, mem.GetWeak()));
        (void)cpu.reset();
        cpu.launch();
    }

    void load_one(uint16_t insn, addr_t at = 0) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&insn);
        (void)mem.load(at, {bytes, sizeof(insn)});
    }
};

} // namespace

// B1: dispatch floor — `B .` (branch-to-self, 0xE7FE). Each step fetches the
// halfword, decodes, dispatches through execute_16bit, and writes PC back to
// itself. Cheapest possible instruction, so ns/iter is the floor any
// per-insn optimization (translation cache / JIT) could approach. SetItems-
// Processed makes GB report insn/s, directly comparable to bench_sim.
static void BM_DispatchBranchSelf(benchmark::State& state) {
    BareCore c;
    c.load_one(0xE7FE); // B .
    (void)c.cpu.set_pc(0);
    perf_stats::reset();
    for (auto _ : state) {
        (void)c.cpu.step();
    }
    state.SetItemsProcessed(state.iterations());
    perf_stats::dump(); // stderr — fetches/instr, bus_reads/instr
}
BENCHMARK(BM_DispatchBranchSelf);

// B5: execute-handler cost — a 2-instruction ALU loop (ADDS r0,#1; B back).
// Each loop iteration runs one dataproc instruction (t16_imm8_dataops →
// decode + update_flags + wr) and one branch. Bare core still has no
// NVIC/SCB wired, so this isolates the dataproc execute-handler tax over
// the B. floor — exactly the work a translation cache would skip.
// (B5-B1)·2 ≈ the per-ALU-handler marginal cost vs a pure branch.
static void BM_DispatchAluLoop(benchmark::State& state) {
    BareCore c;
    c.load_one(0x3001, 0); // ADDS r0,#1  (t16_imm8_dataops)
    c.load_one(0xE7FD, 2); // B -6 → addr 0
    (void)c.cpu.set_pc(0);
    perf_stats::reset();
    for (auto _ : state) {
        (void)c.cpu.step();
    }
    state.SetItemsProcessed(state.iterations());
    perf_stats::dump();
}
BENCHMARK(BM_DispatchAluLoop);

// B6: interrupt-check tax — same B. loop as B1, but with NVIC + SCB wired
// (the end-to-end configuration). check_and_handle_interrupt() now runs
// every step: nvic_->has_pending_irq() scans 8 ISPR/ISER words even when
// empty (it has NO cache, unlike highest_priority_pending_irq). (B6-B1) is
// the per-step interrupt-poll tax that bare core skips by leaving nvic_ null.
static void BM_DispatchWithNvic(benchmark::State& state) {
    BareCore c;
    c.load_one(0xE7FE);
    (void)c.cpu.set_pc(0);
    periph::NvicPeripheral nvic;
    periph::ScbPeripheral scb;
    c.cpu.set_nvic(nvic);
    c.cpu.set_scb(scb);
    for (auto _ : state) {
        (void)c.cpu.step();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_DispatchWithNvic);

// B7: coordinator per-step overhead — B6's config PLUS a SimulationCoord-
// inator driving the step. Each step adds: cpu_ WeakPtr IsValid + deref,
// cpu_->cycles() (another WeakPtr deref + virtual), clock_.advance, and a
// tickables_ scan (empty here). run() would add one more cpu_->state().
// (B7-B6) is the coordinator coordination tax every end-to-end step pays.
static void BM_CoordinatorStep(benchmark::State& state) {
    BareCore c;
    c.load_one(0xE7FE);
    (void)c.cpu.set_pc(0);
    periph::NvicPeripheral nvic;
    periph::ScbPeripheral scb;
    c.cpu.set_nvic(nvic);
    c.cpu.set_scb(scb);
    constexpr std::array<sim::DomainConfig, 1> domains{{{72'000'000}}};
    sim::VirtualClock clock{domains};
    sim::SimulationCoordinator coord{std::move(clock)};
    coord.set_cpu(&c.cpu);
    for (auto _ : state) {
        (void)coord.step();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CoordinatorStep);

// B2: bus tax — one Bus::read against a single mapped FlatMemory. find_region
// (1-region linear scan) + WeakPtr IsValid + Device virtual dispatch + the
// byte-wise FlatMemory::read all live here. Every instruction pays this at
// least once (the fetch); load/store pay it again.
static void BM_BusReadFlat(benchmark::State& state) {
    FlatMemory mem(4096);
    Bus bus;
    (void)bus.map(region(0, 4096, mem.GetWeak()));
    (void)bus.read(0x100, Width::Word); // warm find_region, not timed
    for (auto _ : state) {
        auto r = bus.read(0x100, Width::Word);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_BusReadFlat);

// B3: SysTick tick — the cost the coordinator pushes every step to advance
// the countdown timer. LOAD=0xFFFFFF, ENABLE set so val_>cycles and the
// common early-exit branch (`val_ -= cycles; break;`) runs every call.
// This is the line item P1 ("event-driven timer") rewrites.
static void BM_SysTickTick(benchmark::State& state) {
    periph::SysTickPeripheral systick;
    (void)systick.write(0x04, 0x00FFFFFFu, Width::Word); // LOAD
    (void)systick.write(0x00, 0x1u, Width::Word);        // ENABLE (loads VAL)
    for (auto _ : state) {
        systick.tick(1);
    }
}
BENCHMARK(BM_SysTickTick);

// B4: bus tax scaling — Bus::read against the LAST of N equally-sized
// regions, forcing find_region's linear scan to walk the whole table. Arg
// is the region count: 1 ≈ bare-core, 8..16 ≈ a real SoC (flash + sram +
// several peripheral blocks). Each region also adds a WeakPtr IsValid on
// the way past it. This isolates how the scan + WeakPtr overhead grows.
static void BM_BusReadMultiRegion(benchmark::State& state) {
    const int nregion = static_cast<int>(state.range(0));
    std::vector<std::unique_ptr<FlatMemory>> mems;
    mems.reserve(static_cast<size_t>(nregion));
    for (int i = 0; i < nregion; ++i) {
        mems.push_back(std::make_unique<FlatMemory>(1024));
    }
    Bus bus;
    for (int i = 0; i < nregion; ++i) {
        (void)bus.map(region(static_cast<addr_t>(i) * 1024u, 1024,
                             mems[static_cast<size_t>(i)]->GetWeak()));
    }
    const addr_t target = static_cast<addr_t>(nregion - 1) * 1024u + 0x100;
    (void)bus.read(target, Width::Word); // warm, not timed
    for (auto _ : state) {
        auto r = bus.read(target, Width::Word);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_BusReadMultiRegion)->Arg(1)->Arg(4)->Arg(8)->Arg(16);

BENCHMARK_MAIN();
