/// @file test_execute.cpp
/// @brief Batch-3 acceptance: the bare client runs end to end on the
///        executor (exit code 170 = success path through ALU, branches,
///        memory, push/pop, call/ret), and two identical runs produce a
///        bit-identical instruction trace (determinism contract).

#include <gtest/gtest.h>

#include <micro_forge/arch/x86_64/cpu.hpp>
#include <micro_forge/kernel/bus/bus.hpp>
#include <micro_forge/kernel/memory/ram_region.hpp>
#include <micro_forge/kernel/scheduler.hpp>

#include <cstdint>
#include <fstream>
#include <functional>
#include <tuple>
#include <vector>

namespace {

using micro_forge::arch::x86_64::Cpu;
using micro_forge::arch::x86_64::ExitStatus;
using micro_forge::arch::x86_64::FaultId;
using micro_forge::arch::x86_64::Reg;
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

/// Drives raw instruction bytes at a scratch machine, one step.
struct NewKernelHarness {
    Bus bus;
    RamRegion ram{0x200000}; // code base 0x100000 must be inside RAM
    Scheduler scheduler;
    Cpu cpu;
    NewKernelHarness() : cpu(bus, scheduler) {
        if (!bus.map(0, ram.size(), ram).has_value()) {
            std::abort();
        }
        std::ranges::fill(std::span{ram.data(), ram.size()}, uint8_t{0});
    }
    /// Places @p code at the code base and resets to it.
    void load_code(std::initializer_list<uint8_t> code) {
        std::ranges::copy(code, ram.data() + kCodeBase);
        cpu.reset(kCodeBase, kStackTop);
    }
    /// One step, discarding the (asserted-on-separately) status.
    void step() { (void)cpu.step(); }
};

TEST(NewKernelInsnTest, BswapSwapsBytesAtEveryWidth) {
    NewKernelHarness h;
    // bswap eax (0F C8): 32-bit lane swap.
    // bswap r12 (49 0F CC with REX.W+B): full 64-bit swap.
    // bswap r13w (66 41 0F CD): 16-bit swap inside the lane.
    h.load_code({0x0F, 0xC8, 0x49, 0x0F, 0xCC, 0x66, 0x41, 0x0F, 0xCD});
    // load_code resets the cpu, so seed the registers after it.
    h.cpu.set_gpr(Reg::kRax, 0x12345678'9ABCDEF0);
    h.cpu.set_gpr(Reg::kR12, 0x12345678'9ABCDEF0);
    h.cpu.set_gpr(Reg::kR13, 0x00000000'00003412);
    h.step();
    EXPECT_EQ(static_cast<uint32_t>(h.cpu.gpr(Reg::kRax)), 0xF0DEBC9AU);
    h.step();
    EXPECT_EQ(h.cpu.gpr(Reg::kR12), 0xF0DEBC9A'78563412U);
    h.step();
    EXPECT_EQ(static_cast<uint16_t>(h.cpu.gpr(Reg::kR13)), 0x1234U);
}

TEST(NewKernelInsnTest, PsadbwSumsAbsoluteByteDifferences) {
    NewKernelHarness h;
    // Bytes 0..15 in xmm0, zeros in xmm1: lane sums 28 and 92-28=64+...=92.
    uint64_t lo = 0;
    uint64_t hi = 0;
    for (unsigned i = 0; i < 8; ++i) {
        lo |= static_cast<uint64_t>(i) << (8U * i);
        hi |= static_cast<uint64_t>(i + 8U) << (8U * i);
    }
    h.cpu.set_xmm(Reg::kXmm0, {lo, hi});
    h.cpu.set_xmm(Reg::kXmm1, {0, 0});
    h.load_code({0x66, 0x0F, 0xF6, 0xC1}); // psadbw xmm0, xmm1
    h.step();
    const auto out = h.cpu.xmm(Reg::kXmm0);
    EXPECT_EQ(out[0], 28U); // 0+1+..+7
    EXPECT_EQ(out[1], 92U); // 8+9+..+15
}

TEST(NewKernelInsnTest, PextrwSelectsWord) {
    NewKernelHarness h;
    h.cpu.set_xmm(Reg::kXmm1, {0x0003'0002'0001'0007ULL, 0});
    h.load_code({0x66, 0x0F, 0xC5, 0xC1, 0x01}); // pextrw eax, xmm1, 1
    h.step();
    EXPECT_EQ(h.cpu.gpr(Reg::kRax), 0x1U); // word 2 = 0x0001
}

TEST(NewKernelInsnTest, MovssMergeKeepsDestinationHighBits) {
    NewKernelHarness h;
    h.cpu.set_xmm(Reg::kXmm0, {0x0F0F'0F0F'0F0F'0F0FULL, 0x0F0F'0F0F'0F0F'0F0FULL});
    h.cpu.set_xmm(Reg::kXmm1, {0x1234'5678'9ABC'DEF0ULL, 0x5555'5555'5555'5555ULL});
    h.load_code({0xF3, 0x0F, 0x10, 0xC1}); // movss xmm0, xmm1
    h.step();
    const auto out = h.cpu.xmm(Reg::kXmm0);
    EXPECT_EQ(out[0], 0x0F0F'0F0F'9ABC'DEF0ULL); // low 32 replaced
    EXPECT_EQ(out[1], 0x0F0F'0F0F'0F0F'0F0FULL); // upper 96 untouched
}

TEST(NewKernelInsnTest, MovssStoreWritesFourBytes) {
    NewKernelHarness h;
    const uint64_t dst = 0x80000;
    const uint64_t imm = dst;
    const uint64_t val = 0x0FFF'FFFF'1234'5678ULL;
    // movabs rax, val; movq xmm0, rax; movabs rax, dst; movss [rax], xmm0.
    h.load_code({
        0x48,
        0xB8,
        static_cast<uint8_t>(val),
        static_cast<uint8_t>(val >> 8U),
        static_cast<uint8_t>(val >> 16U),
        static_cast<uint8_t>(val >> 24U),
        static_cast<uint8_t>(val >> 32U),
        static_cast<uint8_t>(val >> 40U),
        static_cast<uint8_t>(val >> 48U),
        static_cast<uint8_t>(val >> 56U),
        0x66,
        0x48,
        0x0F,
        0x6E,
        0xC0,
        0x48,
        0xB8,
        static_cast<uint8_t>(imm),
        static_cast<uint8_t>(imm >> 8U),
        static_cast<uint8_t>(imm >> 16U),
        static_cast<uint8_t>(imm >> 24U),
        static_cast<uint8_t>(imm >> 32U),
        static_cast<uint8_t>(imm >> 40U),
        static_cast<uint8_t>(imm >> 48U),
        static_cast<uint8_t>(imm >> 56U),
        0xF3,
        0x0F,
        0x11,
        0x00,
    }); // movss [rax], xmm0
    h.step();
    h.step();
    h.step();
    h.step();
    uint64_t got = 0;
    for (unsigned i = 0; i < 4; ++i) {
        got |= static_cast<uint64_t>(h.ram.data()[dst + i]) << (8U * i);
    }
    EXPECT_EQ(got, 0x1234'5678ULL);
}

TEST(NewKernelInsnTest, ClacStacToggleAlignmentCheck) {
    NewKernelHarness h;
    // stac then clac, in sequence (decode cache keys on rip).
    h.load_code({0x0F, 0x01, 0xCB, 0x0F, 0x01, 0xCA});
    h.step(); // stac
    EXPECT_NE(h.cpu.rflags() & (1ULL << 18U), 0U);
    h.step(); // clac
    EXPECT_EQ(h.cpu.rflags() & (1ULL << 18U), 0U);
}

TEST(NewKernelInsnTest, FninitRetiresQuietly) {
    NewKernelHarness h;
    h.load_code({0xDB, 0xE3});
    const auto st = h.cpu.step();
    EXPECT_EQ(st.kind, ExitStatus::Kind::kRunning);
}

TEST(NewKernelInsnTest, Ud2RaisesUndefinedFault) {
    NewKernelHarness h;
    h.load_code({0x0F, 0x0B});
    const auto st = h.cpu.step();
    ASSERT_EQ(st.kind, ExitStatus::Kind::kFault);
    EXPECT_EQ(st.detail, static_cast<uint8_t>(FaultId::kUd));
}

} // namespace
