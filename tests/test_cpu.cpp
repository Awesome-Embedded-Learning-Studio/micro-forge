/// @file test_cpu.cpp
/// @brief Batch-3 executor unit tests: byte programs assembled and
///        verified against GNU as (encodings cross-checked with objdump).

#include "cpu_fixture.hpp"

namespace {

TEST_F(CpuTest, MovImm32ZeroExtendsToRax) {
    load({0xB8, 0x78, 0x56, 0x34, 0x12}); // mov eax, 0x12345678
    (void)cpu_.run(10);
    EXPECT_EQ(0x12345678U, cpu_.gpr(Reg::kRax));
    EXPECT_EQ(ExitStatus::Kind::kHalt, cpu_.exit_status().kind); // trailing hlt
}

TEST_F(CpuTest, MovabsCarriesImm64) {
    load({0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11});
    (void)cpu_.run(10);
    EXPECT_EQ(0x1122334455667788ULL, cpu_.gpr(Reg::kRax));
}

TEST_F(CpuTest, CachedCodeIsInvalidatedAfterRepeatedGuestStores) {
    cpu_.configure_caches(ram_.size());
    load({
        0xB8,
        0x01,
        0x00,
        0x00,
        0x00, // mov eax, 1
        0x88,
        0x17, // mov [rdi], dl
    });
    cpu_.set_gpr(Reg::kRdi, kCodeBase + 1);
    (void)cpu_.step(); // decode and mark the instruction's physical page
    for (uint64_t value = 2; value <= 3; ++value) {
        const auto hits = cpu_.decode_cache_stats().hits;
        cpu_.set_rip(kCodeBase);
        (void)cpu_.step(); // cache hit must retain self-modifying-code tracking
        EXPECT_EQ(hits + 1, cpu_.decode_cache_stats().hits);
        cpu_.set_gpr(Reg::kRdx, value);
        cpu_.set_rip(kCodeBase + 5);
        (void)cpu_.step(); // guest rewrites the cached mov's immediate
        cpu_.set_rip(kCodeBase);
        (void)cpu_.step();
        EXPECT_EQ(value, cpu_.gpr(Reg::kRax));
        EXPECT_EQ(ExitStatus::Kind::kRunning, cpu_.exit_status().kind);
    }
}

TEST_F(CpuTest, AddByteWrapsWithCarry) {
    load({0xB0, 0xFF, 0x04, 0x01}); // mov al, 0xFF; add al, 1
    (void)cpu_.run(10);
    EXPECT_EQ(0x00U, cpu_.gpr(Reg::kRax) & 0xFFU);
    EXPECT_TRUE(cpu_.cf());
    EXPECT_TRUE(cpu_.zf());
    EXPECT_TRUE(cpu_.af());
    EXPECT_FALSE(cpu_.of());
}

TEST_F(CpuTest, SubFromZeroSetsBorrowAndOverflow) {
    load({0x31, 0xC0, 0x2D, 0x00, 0x00, 0x00, 0x80}); // xor eax,eax; sub eax, 0x80000000
    (void)cpu_.run(10);
    EXPECT_EQ(0x80000000U, static_cast<uint32_t>(cpu_.gpr(Reg::kRax)));
    EXPECT_TRUE(cpu_.cf());
    EXPECT_TRUE(cpu_.sf());
    EXPECT_TRUE(cpu_.of());
}

TEST_F(CpuTest, AdcConsumesCarry) {
    load({0xB0, 0xFF, 0x04, 0x01, 0x14, 0x00}); // al=0xFF; add al,1; adc al,0
    (void)cpu_.run(10);
    EXPECT_EQ(0x01U, cpu_.gpr(Reg::kRax) & 0xFFU);
    EXPECT_FALSE(cpu_.cf());
}

TEST_F(CpuTest, IncPreservesCarry) {
    load({0xB0, 0xFF, 0x04, 0x01, 0xFE, 0xC0}); // al=0xFF; add al,1; inc al
    (void)cpu_.run(10);
    EXPECT_EQ(0x01U, cpu_.gpr(Reg::kRax) & 0xFFU); // 0 + 1 after the wrap
    EXPECT_TRUE(cpu_.cf());                        // inc must not touch CF
    EXPECT_FALSE(cpu_.zf());
}

TEST_F(CpuTest, NegSetsCarryOnNonzero) {
    load({0xB0, 0x01, 0xF6, 0xD8}); // mov al,1; neg al
    (void)cpu_.run(10);
    EXPECT_EQ(0xFFU, cpu_.gpr(Reg::kRax) & 0xFFU);
    EXPECT_TRUE(cpu_.cf());
    EXPECT_TRUE(cpu_.sf());
}

TEST_F(CpuTest, MovsxSignExtendsThroughWidths) {
    load({0xB3, 0x80, 0x48, 0x0F, 0xBE, 0xC3}); // mov bl,0x80; movsx rax, bl
    (void)cpu_.run(10);
    EXPECT_EQ(0xFFFFFFFFFFFFFF80ULL, cpu_.gpr(Reg::kRax));
}

TEST_F(CpuTest, MovzxThenShift) {
    load({0x44, 0x0F, 0xB6, 0x06, 0x49, 0xC1, 0xE0, 0x02}); // movzx r8d,[rsi]; shl r8,2
    poke(0x2000, {0x80});
    cpu_.reset(kCodeBase, kStackTop);
    cpu_.set_gpr(Reg::kRsi, 0x2000);
    (void)cpu_.run(10);
    EXPECT_EQ(0x200ULL, cpu_.gpr(Reg::kR8));
}

TEST_F(CpuTest, PushPopRoundTripRestoresStack) {
    load({0x53, 0x5B}); // push rbx; pop rbx
    cpu_.set_gpr(Reg::kRbx, 0xAA55);
    (void)cpu_.run(10);
    EXPECT_EQ(0xAA55ULL, cpu_.gpr(Reg::kRbx));
    EXPECT_EQ(kStackTop, cpu_.gpr(Reg::kRsp));
}

TEST_F(CpuTest, XchgSwapsAccumulator) {
    load({0x91}); // xchg eax, ecx
    cpu_.set_gpr(Reg::kRax, 1);
    cpu_.set_gpr(Reg::kRcx, 2);
    (void)cpu_.run(10);
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
    (void)cpu_.run(100);
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
    (void)cpu_.run(10);
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
    (void)cpu_.run(100);
    EXPECT_EQ(ExitStatus::Kind::kDebugExit, cpu_.exit_status().kind);
    EXPECT_EQ(7U, cpu_.exit_status().code); // (3 << 1) | 1
}

TEST_F(CpuTest, CpuidAnswersLeafZero) {
    load({0x0F, 0xA2}); // cpuid
    (void)cpu_.run(10);
    EXPECT_EQ(ExitStatus::Kind::kHalt, cpu_.exit_status().kind);
    // Leaf 0 names the vendor and the highest standard leaf.
    EXPECT_EQ(0x0000000DU, static_cast<uint32_t>(cpu_.gpr(Reg::kRax)));
    EXPECT_EQ(0x756E6547U, static_cast<uint32_t>(cpu_.gpr(Reg::kRbx)));
    EXPECT_EQ(0x6C65746EU, static_cast<uint32_t>(cpu_.gpr(Reg::kRcx)));
    EXPECT_EQ(0x49656E69U, static_cast<uint32_t>(cpu_.gpr(Reg::kRdx)));
}

TEST_F(CpuTest, UnmappedLoadReportsFaultAddress) {
    load({0x48, 0xA1, 0x00, 0x00, 0xAD, 0xDE, 0x00, 0x00, 0x00, 0x00}); // mov rax, [0xDEAD0000]
    (void)cpu_.run(10);
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
    (void)cpu_.run(100);
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
    (void)cpu_.run(10);
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
    (void)cpu_.run(100);
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
    (void)cpu_.run(100);
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
    (void)cpu_.run(100);
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
    (void)cpu_.run(100);
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
    (void)cpu_.run(100);
    EXPECT_EQ(ExitStatus::Kind::kFault, cpu_.exit_status().kind);
    EXPECT_EQ(static_cast<uint64_t>(FaultId::kDivide), cpu_.exit_status().detail);
}

TEST_F(CpuTest, ImulThreeOperandTruncates) {
    load({0x48, 0x6B, 0xC0, 0x07}); // imul rax, rax, 7
    cpu_.set_gpr(Reg::kRax, 5);
    (void)cpu_.run(10);
    EXPECT_EQ(35U, cpu_.gpr(Reg::kRax));
}

TEST_F(CpuTest, WriteSyscallAdvancesRip) {
    // Batch-5 review: the write path used to leave rip on the syscall,
    // re-executing it forever (tests passed by walking into the
    // unknown-syscall branch). A following instruction must run, once.
    load({
        0xB8, 0x01, 0x00, 0x00, 0x00,             // mov eax, 1 (write)
        0xBF, 0x01, 0x00, 0x00, 0x00,             // mov edi, 1
        0x48, 0xC7, 0xC6, 0x00, 0x20, 0x00, 0x00, // mov rsi, 0x2000
        0xBA, 0x02, 0x00, 0x00, 0x00,             // mov edx, 2
        0x0F, 0x05,                               // syscall
        0xBB, 0x42, 0x00, 0x00, 0x00,
    }); // mov ebx, 0x42
    poke(0x2000, {'a', 'b'});
    cpu_.reset(kCodeBase, kStackTop);
    cpu_.set_gpr(Reg::kRdi, 1);
    cpu_.set_gpr(Reg::kRsi, 0x2000);
    cpu_.set_gpr(Reg::kRdx, 2);
    cpu_.set_gpr(Reg::kRax, 1);
    // run from scratch: reset already set rip; the program sets regs
    // itself, so only the poke mattered.
    (void)cpu_.run(100);
    EXPECT_EQ(ExitStatus::Kind::kHalt, cpu_.exit_status().kind);
    EXPECT_EQ(0x42U, static_cast<uint32_t>(cpu_.gpr(Reg::kRbx)));
    const std::vector<char> expected{'a', 'b'};
    EXPECT_EQ(expected, cpu_.console()); // exactly once, no loop
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
    (void)cpu_.run(100);
    const std::vector<char> expected{'a', 'b', 'c'};
    EXPECT_EQ(expected, cpu_.console());
    EXPECT_EQ(3U, cpu_.gpr(Reg::kRax)); // write returns the byte count
}

TEST_F(CpuTest, BtImmReadsWithoutWriting) {
    // 0F BA /4 is bt: it must NOT modify memory -- before the batch-4
    // review the map was off by one and every bt executed as bts.
    load({0x0F, 0xBA, 0x27, 0x02}); // bt DWORD PTR [rdi], 2
    cpu_.set_gpr(Reg::kRdi, 0x2000);
    ram_.data()[0x2000] = 0x04; // bit 2 set
    cpu_.reset(kCodeBase, kStackTop);
    cpu_.set_gpr(Reg::kRdi, 0x2000);
    (void)cpu_.run(10);
    EXPECT_EQ(ExitStatus::Kind::kHalt, cpu_.exit_status().kind);
    EXPECT_TRUE(cpu_.cf());
    EXPECT_EQ(0x04U, static_cast<unsigned>(ram_.data()[0x2000])); // untouched
}

TEST_F(CpuTest, BtsImmWritesTheBit) {
    load({0x0F, 0xBA, 0x2F, 0x01}); // bts DWORD PTR [rdi], 1
    poke(0x2000, {0x00});
    cpu_.reset(kCodeBase, kStackTop);
    cpu_.set_gpr(Reg::kRdi, 0x2000);
    (void)cpu_.run(10);
    EXPECT_EQ(0x02U, static_cast<unsigned>(ram_.data()[0x2000]));
    EXPECT_FALSE(cpu_.cf()); // CF holds the OLD bit value: it was clear
}

TEST_F(CpuTest, BtMemImm8UsesSignedByteOffset) {
    // 48 0F BA 20 80: btq [rax], -128 -- the imm8 form sign-extends from
    // one byte, addressing rax-16 (batch-4 review: the 64-bit form used
    // to shift the raw byte, landing at rax+16).
    load({0x48, 0x0F, 0xBA, 0x20, 0x80});
    cpu_.reset(kCodeBase, kStackTop);
    cpu_.set_gpr(Reg::kRax, 0x2010);
    ram_.data()[0x2000] = 0x01; // rax-16, bit 0 (0x80 & 7): set
    (void)cpu_.run(10);
    EXPECT_TRUE(cpu_.cf());
}

TEST_F(CpuTest, Shld16BitLargeCountStaysDefined) {
    // 66 0F A4 C0 11: shld ax, ax, 17. Counts >= the operand width leave
    // the result architecturally undefined; the executor clamps so the
    // shifts stay in defined C++ territory (used to be UB by underflow).
    load({0x66, 0x0F, 0xA4, 0xC0, 0x11});
    cpu_.reset(kCodeBase, kStackTop);
    cpu_.set_gpr(Reg::kRax, 0xBEEF);
    (void)cpu_.run(10);
    EXPECT_EQ(ExitStatus::Kind::kHalt, cpu_.exit_status().kind);
    // Undefined on hardware; our clamp picks shift-by-15, so with
    // ax=0xBEEF on both sides: (0xBEEF << 15) | (0xBEEF >> 1) in 16 bits
    // = 0xDF77. Asserting it documents the clamp choice; the sanitizer
    // gate checks the harder property (no UB at all).
    EXPECT_EQ(0xDF77U, static_cast<unsigned>(cpu_.gpr(Reg::kRax) & 0xFFFFU));
}

TEST_F(CpuTest, StringOpUnderFsGsIsRefused) {
    load({0x65, 0xF3, 0xA4}); // gs rep movsb
    (void)cpu_.run(10);
    EXPECT_EQ(ExitStatus::Kind::kUnsupported, cpu_.exit_status().kind);
}

TEST_F(CpuTest, FetchFromUnmappedRipIsAFault) {
    load({0x90});
    cpu_.reset(0xDEAD0000ULL, kStackTop); // nothing mapped there
    (void)cpu_.run(10);
    EXPECT_EQ(ExitStatus::Kind::kFault, cpu_.exit_status().kind);
    EXPECT_EQ(static_cast<uint64_t>(FaultId::kFetch), cpu_.exit_status().detail);
    EXPECT_EQ(0U, cpu_.icount()); // the faulting fetch retires nothing
}

TEST_F(CpuTest, DecodeRefusalSurfacesAsDecodeError) {
    // 0F FF is an undefined escape the whitelist refuses.
    load({0x0F, 0xFF});
    (void)cpu_.run(10);
    EXPECT_EQ(ExitStatus::Kind::kDecodeError, cpu_.exit_status().kind);
}

} // namespace
