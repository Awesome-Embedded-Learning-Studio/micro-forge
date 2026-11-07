/// @file test_cpu.cpp
/// @brief Batch-3 executor unit tests: byte programs assembled and
///        verified against GNU as (encodings cross-checked with objdump).

#include <gtest/gtest.h>

#include <micro_forge/arch/x86_64/cpu.hpp>
#include <micro_forge/arch/x86_64/decoded.hpp>
#include <micro_forge/kernel/bus.hpp>
#include <micro_forge/kernel/debug_exit.hpp>
#include <micro_forge/kernel/ram_region.hpp>
#include <micro_forge/kernel/scheduler.hpp>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace {

using micro_forge::arch::x86_64::Cpu;
using micro_forge::arch::x86_64::ExitStatus;
using micro_forge::arch::x86_64::FaultId;
using micro_forge::arch::x86_64::Mnemonic;
using micro_forge::arch::x86_64::Reg;
using micro_forge::kernel::Bus;
using micro_forge::kernel::DebugExitRegion;
using micro_forge::kernel::RamRegion;
using micro_forge::kernel::Scheduler;

constexpr uint64_t kCodeBase = 0x100000;
constexpr uint64_t kStackTop = 0x90000;

/// One flat 16 MiB machine: RAM at 0 (code at 0x100000, stack at
/// 0x90000), plus the batch-3 CPU. Tests load small byte programs and
/// inspect registers, flags, memory, and the exit status.
class CpuTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(bus_.map(0, ram_.size(), ram_).has_value());
        cpu_.reset(kCodeBase, kStackTop);
    }

    /// Copies a byte program to the code base and re-arms the CPU. A
    /// trailing hlt terminates every program: without it the executor
    /// walks into zero-filled RAM, which decodes as `add [rax], al` and
    /// scrambles the state under test.
    /// Register setup happens after this call, before run().
    void load(std::initializer_list<uint8_t> code) {
        std::ranges::copy(code, ram_.data() + kCodeBase);
        ram_.data()[kCodeBase + code.size()] = 0xF4;
        cpu_.reset(kCodeBase, kStackTop);
    }

    void poke(uint64_t address, std::initializer_list<uint8_t> bytes) {
        std::ranges::copy(bytes, ram_.data() + address);
    }

    Bus bus_;
    RamRegion ram_{0x1000000};
    Scheduler scheduler_;
    Cpu cpu_{bus_, scheduler_};
};

TEST_F(CpuTest, MovImm32ZeroExtendsToRax) {
    load({0xB8, 0x78, 0x56, 0x34, 0x12}); // mov eax, 0x12345678
    cpu_.run(10);
    EXPECT_EQ(0x12345678U, cpu_.gpr(Reg::kRax));
    EXPECT_EQ(ExitStatus::Kind::kHalt, cpu_.exit_status().kind); // trailing hlt
}

TEST_F(CpuTest, MovabsCarriesImm64) {
    load({0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11});
    cpu_.run(10);
    EXPECT_EQ(0x1122334455667788ULL, cpu_.gpr(Reg::kRax));
}

TEST_F(CpuTest, AddByteWrapsWithCarry) {
    load({0xB0, 0xFF, 0x04, 0x01}); // mov al, 0xFF; add al, 1
    cpu_.run(10);
    EXPECT_EQ(0x00U, cpu_.gpr(Reg::kRax) & 0xFFU);
    EXPECT_TRUE(cpu_.cf());
    EXPECT_TRUE(cpu_.zf());
    EXPECT_TRUE(cpu_.af());
    EXPECT_FALSE(cpu_.of());
}

TEST_F(CpuTest, SubFromZeroSetsBorrowAndOverflow) {
    load({0x31, 0xC0, 0x2D, 0x00, 0x00, 0x00, 0x80}); // xor eax,eax; sub eax, 0x80000000
    cpu_.run(10);
    EXPECT_EQ(0x80000000U, static_cast<uint32_t>(cpu_.gpr(Reg::kRax)));
    EXPECT_TRUE(cpu_.cf());
    EXPECT_TRUE(cpu_.sf());
    EXPECT_TRUE(cpu_.of());
}

TEST_F(CpuTest, AdcConsumesCarry) {
    load({0xB0, 0xFF, 0x04, 0x01, 0x14, 0x00}); // al=0xFF; add al,1; adc al,0
    cpu_.run(10);
    EXPECT_EQ(0x01U, cpu_.gpr(Reg::kRax) & 0xFFU);
    EXPECT_FALSE(cpu_.cf());
}

TEST_F(CpuTest, IncPreservesCarry) {
    load({0xB0, 0xFF, 0x04, 0x01, 0xFE, 0xC0}); // al=0xFF; add al,1; inc al
    cpu_.run(10);
    EXPECT_EQ(0x01U, cpu_.gpr(Reg::kRax) & 0xFFU); // 0 + 1 after the wrap
    EXPECT_TRUE(cpu_.cf());                        // inc must not touch CF
    EXPECT_FALSE(cpu_.zf());
}

TEST_F(CpuTest, NegSetsCarryOnNonzero) {
    load({0xB0, 0x01, 0xF6, 0xD8}); // mov al,1; neg al
    cpu_.run(10);
    EXPECT_EQ(0xFFU, cpu_.gpr(Reg::kRax) & 0xFFU);
    EXPECT_TRUE(cpu_.cf());
    EXPECT_TRUE(cpu_.sf());
}

TEST_F(CpuTest, MovsxSignExtendsThroughWidths) {
    load({0xB3, 0x80, 0x48, 0x0F, 0xBE, 0xC3}); // mov bl,0x80; movsx rax, bl
    cpu_.run(10);
    EXPECT_EQ(0xFFFFFFFFFFFFFF80ULL, cpu_.gpr(Reg::kRax));
}

TEST_F(CpuTest, MovzxThenShift) {
    load({0x44, 0x0F, 0xB6, 0x06, 0x49, 0xC1, 0xE0, 0x02}); // movzx r8d,[rsi]; shl r8,2
    poke(0x2000, {0x80});
    cpu_.reset(kCodeBase, kStackTop);
    cpu_.set_gpr(Reg::kRsi, 0x2000);
    cpu_.run(10);
    EXPECT_EQ(0x200ULL, cpu_.gpr(Reg::kR8));
}

TEST_F(CpuTest, PushPopRoundTripRestoresStack) {
    load({0x53, 0x5B}); // push rbx; pop rbx
    cpu_.set_gpr(Reg::kRbx, 0xAA55);
    cpu_.run(10);
    EXPECT_EQ(0xAA55ULL, cpu_.gpr(Reg::kRbx));
    EXPECT_EQ(kStackTop, cpu_.gpr(Reg::kRsp));
}

TEST_F(CpuTest, XchgSwapsAccumulator) {
    load({0x91}); // xchg eax, ecx
    cpu_.set_gpr(Reg::kRax, 1);
    cpu_.set_gpr(Reg::kRcx, 2);
    cpu_.run(10);
    EXPECT_EQ(2U, static_cast<uint32_t>(cpu_.gpr(Reg::kRax)));
    EXPECT_EQ(1U, static_cast<uint32_t>(cpu_.gpr(Reg::kRcx)));
}

TEST_F(CpuTest, RepStosbFillsAndAdvances) {
    load({
        0x48,
        0xC7,
        0xC7,
        0x00,
        0x10,
        0x00,
        0x00, // mov rdi, 0x1000
        0x48,
        0xC7,
        0xC1,
        0x05,
        0x00,
        0x00,
        0x00, // mov rcx, 5
        0xB0,
        0x77, // mov al, 0x77
        0xF3,
        0xAA,
    }); // rep stosb
    cpu_.run(100);
    EXPECT_EQ(0U, cpu_.gpr(Reg::kRcx));
    EXPECT_EQ(0x1005ULL, cpu_.gpr(Reg::kRdi));
    for (uint64_t a = 0x1000; a < 0x1005; ++a) {
        EXPECT_EQ(0x77U, static_cast<unsigned>(ram_.data()[a])) << "address " << a;
    }
    // One iteration per retired instruction: setup 3 + 5 stores + hlt.
    EXPECT_EQ(9U, cpu_.icount());
}

TEST_F(CpuTest, DirectionFlagWalksBackward) {
    load({0xFD, 0xA4, 0xFC}); // std; movsb; cld
    poke(0x2000, {0x5A});
    cpu_.set_gpr(Reg::kRsi, 0x2000);
    cpu_.set_gpr(Reg::kRdi, 0x1000);
    cpu_.run(10);
    EXPECT_EQ(0x5AU, static_cast<unsigned>(ram_.data()[0x1000]));
    EXPECT_EQ(0x1FFFULL, cpu_.gpr(Reg::kRsi));
    EXPECT_EQ(0xFFFULL, cpu_.gpr(Reg::kRdi));
    EXPECT_FALSE(cpu_.df());
}

TEST_F(CpuTest, DebugExitRegionStopsRunWithQemuEncoding) {
    DebugExitRegion exit_region([this](uint32_t code) { cpu_.request_debug_exit(code); });
    ASSERT_TRUE(bus_.map(0xFEED0000, 0x1000, exit_region).has_value());
    load({
        0x48,
        0xB8,
        0x00,
        0x00,
        0xED,
        0xFE,
        0x00,
        0x00,
        0x00,
        0x00, // movabs rax, 0xFEED0000
        0xC7,
        0x00,
        0x03,
        0x00,
        0x00,
        0x00,
    }); // mov dword ptr [rax], 3
    cpu_.run(100);
    EXPECT_EQ(ExitStatus::Kind::kDebugExit, cpu_.exit_status().kind);
    EXPECT_EQ(7U, cpu_.exit_status().code); // (3 << 1) | 1
}

TEST_F(CpuTest, CpuidIsNamedUnsupported) {
    load({0x0F, 0xA2}); // cpuid
    cpu_.run(10);
    EXPECT_EQ(ExitStatus::Kind::kUnsupported, cpu_.exit_status().kind);
    EXPECT_EQ(kCodeBase, cpu_.exit_status().rip);
    EXPECT_EQ(static_cast<uint64_t>(Mnemonic::kCpuid), cpu_.exit_status().detail);
}

TEST_F(CpuTest, UnmappedLoadReportsFaultAddress) {
    load({0x48, 0xA1, 0x00, 0x00, 0xAD, 0xDE, 0x00, 0x00, 0x00, 0x00}); // mov rax, [0xDEAD0000]
    cpu_.run(10);
    EXPECT_EQ(ExitStatus::Kind::kFault, cpu_.exit_status().kind);
    EXPECT_EQ(static_cast<uint64_t>(FaultId::kMemory), cpu_.exit_status().detail);
    EXPECT_EQ(0xDEAD0000ULL, cpu_.exit_status().code);
}

TEST_F(CpuTest, SyscallExitCarriesRdi) {
    load({
        0xB8,
        0x3C,
        0x00,
        0x00,
        0x00, // mov eax, 60
        0x48,
        0xC7,
        0xC7,
        0x2A,
        0x00,
        0x00,
        0x00, // mov rdi, 42
        0x0F,
        0x05,
    }); // syscall
    cpu_.run(100);
    EXPECT_EQ(ExitStatus::Kind::kExit, cpu_.exit_status().kind);
    EXPECT_EQ(42U, cpu_.exit_status().code);
}

TEST_F(CpuTest, DivideByZeroFaults) {
    load({
        0xB9,
        0x00,
        0x00,
        0x00,
        0x00, // mov ecx, 0
        0xF7,
        0xF1,
    }); // div ecx
    cpu_.run(10);
    EXPECT_EQ(ExitStatus::Kind::kFault, cpu_.exit_status().kind);
    EXPECT_EQ(static_cast<uint64_t>(FaultId::kDivide), cpu_.exit_status().detail);
}

TEST_F(CpuTest, Div64HandlesTrue128BitDividend) {
    // 0x1_0000_0000_0000_0000 / 2 = 2^63: only the shift-subtract path
    // computes this (the old code composed the dividend with rdx << 64,
    // undefined behavior, and divided garbage).
    load({
        0xBA,
        0x01,
        0x00,
        0x00,
        0x00, // mov edx, 1
        0x31,
        0xC0, // xor eax, eax
        0xB9,
        0x02,
        0x00,
        0x00,
        0x00, // mov ecx, 2
        0x48,
        0xF7,
        0xF1,
    }); // div rcx
    cpu_.run(100);
    EXPECT_EQ(ExitStatus::Kind::kHalt, cpu_.exit_status().kind);
    EXPECT_EQ(0x8000000000000000ULL, cpu_.gpr(Reg::kRax));
    EXPECT_EQ(0U, cpu_.gpr(Reg::kRdx));
}

TEST_F(CpuTest, Div64LeavesRemainderInRdx) {
    load({
        0xB8,
        0x05,
        0x00,
        0x00,
        0x00, // mov eax, 5 (dividend low half)
        0x31,
        0xD2, // xor edx, edx
        0xB9,
        0x02,
        0x00,
        0x00,
        0x00, // mov ecx, 2
        0x48,
        0xF7,
        0xF1,
    }); // div rcx
    cpu_.run(100);
    EXPECT_EQ(2U, cpu_.gpr(Reg::kRax));
    EXPECT_EQ(1U, cpu_.gpr(Reg::kRdx));
}

TEST_F(CpuTest, Div64QuotientOverflowFaults) {
    // 0x2_0000...0 / 2 leaves the 64-bit quotient envelope: #DE.
    load({
        0xBA,
        0x02,
        0x00,
        0x00,
        0x00, // mov edx, 2
        0x31,
        0xC0, // xor eax, eax
        0xB9,
        0x02,
        0x00,
        0x00,
        0x00, // mov ecx, 2
        0x48,
        0xF7,
        0xF1,
    }); // div rcx
    cpu_.run(100);
    EXPECT_EQ(ExitStatus::Kind::kFault, cpu_.exit_status().kind);
    EXPECT_EQ(static_cast<uint64_t>(FaultId::kDivide), cpu_.exit_status().detail);
}

TEST_F(CpuTest, Idiv64DividesNegative128BitValue) {
    // s128 -4 / 2 = -2 remainder 0.
    load({
        0x48, 0xC7, 0xC2, 0xFF, 0xFF, 0xFF, 0xFF, // mov rdx, -1
        0x48, 0xC7, 0xC0, 0xFC, 0xFF, 0xFF, 0xFF, // mov rax, -4
        0xB9, 0x02, 0x00, 0x00, 0x00,             // mov ecx, 2
        0x48, 0xF7, 0xF9,
    }); // idiv rcx
    cpu_.run(100);
    EXPECT_EQ(ExitStatus::Kind::kHalt, cpu_.exit_status().kind);
    EXPECT_EQ(0xFFFFFFFFFFFFFFFEULL, cpu_.gpr(Reg::kRax));
    EXPECT_EQ(0U, cpu_.gpr(Reg::kRdx));
}

TEST_F(CpuTest, Idiv64PositiveOverflowFaults) {
    // s128 -2^63 / -1 = +2^63: no positive signed 64-bit slot for it.
    load({
        0x48, 0xC7, 0xC2, 0xFF, 0xFF, 0xFF, 0xFF,                   // mov rdx, -1
        0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, // movabs rax, 2^63
        0x48, 0xC7, 0xC1, 0xFF, 0xFF, 0xFF, 0xFF,                   // mov rcx, -1
        0x48, 0xF7, 0xF9,
    }); // idiv rcx
    cpu_.run(100);
    EXPECT_EQ(ExitStatus::Kind::kFault, cpu_.exit_status().kind);
    EXPECT_EQ(static_cast<uint64_t>(FaultId::kDivide), cpu_.exit_status().detail);
}

TEST_F(CpuTest, ImulThreeOperandTruncates) {
    load({0x48, 0x6B, 0xC0, 0x07}); // imul rax, rax, 7
    cpu_.set_gpr(Reg::kRax, 5);
    cpu_.run(10);
    EXPECT_EQ(35U, cpu_.gpr(Reg::kRax));
}

TEST_F(CpuTest, SyscallWriteCollectsConsole) {
    load({
        0xB8, 0x01, 0x00, 0x00, 0x00,             // mov eax, 1
        0xBF, 0x01, 0x00, 0x00, 0x00,             // mov edi, 1
        0x48, 0xC7, 0xC6, 0x00, 0x20, 0x00, 0x00, // mov rsi, 0x2000
        0xBA, 0x03, 0x00, 0x00, 0x00,             // mov edx, 3
        0x0F, 0x05,
    }); // syscall
    poke(0x2000, {'a', 'b', 'c'});
    cpu_.run(100);
    const std::vector<char> expected{'a', 'b', 'c'};
    EXPECT_EQ(expected, cpu_.console());
    EXPECT_EQ(3U, cpu_.gpr(Reg::kRax)); // write returns the byte count
}

} // namespace
