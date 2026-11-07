/**
 * @file test_decode.cpp
 * @brief Unit tests for the x86-64 decoder (batch 2).
 * @version 0.1.0
 */

#include <gtest/gtest.h>

#include <array>
#include <string>

#include "micro_forge/arch/x86_64/decoder.hpp"

namespace mf = micro_forge::arch::x86_64;

namespace {

/// Decodes bytes and renders text; FAILs with context on decode errors.
std::string text_of(std::initializer_list<uint8_t> bytes, uint64_t rip = 0) {
    const std::vector<uint8_t> v(bytes);
    const auto r = mf::decode_one(v);
    if (!r) {
        ADD_FAILURE() << "decode failed at " << r.error().offset;
        return {};
    }
    return mf::to_text(*r, rip);
}

TEST(DecodeTest, MovRegReg) {
    EXPECT_EQ(text_of({0x48, 0x89, 0xC8}), "mov rax, rcx");
    EXPECT_EQ(text_of({0x89, 0xC8}), "mov eax, ecx");
    EXPECT_EQ(text_of({0x66, 0x89, 0xC8}), "mov ax, cx");
    EXPECT_EQ(text_of({0x88, 0xC8}), "mov al, cl");
}

TEST(DecodeTest, RexExtendedRegs) {
    EXPECT_EQ(text_of({0x4C, 0x89, 0xC0}), "mov rax, r8");
    EXPECT_EQ(text_of({0x4C, 0x89, 0xC1}), "mov rcx, r8");
    EXPECT_EQ(text_of({0x49, 0x89, 0xC1}), "mov r9, rax");
}

TEST(DecodeTest, HighByteRegsNeedRex) {
    // 88 E0: mod=3 reg=4 (ah without REX) rm=0 (al) -> mov al, ah.
    EXPECT_EQ(text_of({0x88, 0xE0}), "mov al, ah");
    // 40 88 E0 with REX: reg=4 becomes spl.
    EXPECT_EQ(text_of({0x40, 0x88, 0xE0}), "mov al, spl");
}

TEST(DecodeTest, MovImm) {
    EXPECT_EQ(text_of({0xB8, 0x78, 0x56, 0x34, 0x12}), "mov eax, 0x12345678");
    EXPECT_EQ(text_of({0x48, 0xC7, 0xC0, 0x78, 0x56, 0x34, 0x12}), "mov rax, 0x12345678");
    // mov r8, imm64.
    EXPECT_EQ(text_of({0x49, 0xB8, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}),
              "movabs r8, 0x807060504030201");
    EXPECT_EQ(text_of({0xB0, 0x7F}), "mov al, 0x7f");
}

TEST(DecodeTest, MemorySibAndDisp) {
    EXPECT_EQ(text_of({0x48, 0x8B, 0x04, 0xC1}), "mov rax, QWORD PTR [rcx+rax*8]");
    EXPECT_EQ(text_of({0x8B, 0x04, 0xC1}), "mov eax, DWORD PTR [rcx+rax*8]");
    // disp8
    EXPECT_EQ(text_of({0x48, 0x8B, 0x45, 0xF8}), "mov rax, QWORD PTR [rbp-0x8]");
    // disp32
    EXPECT_EQ(text_of({0x48, 0x8B, 0x85, 0x00, 0x00, 0x00, 0x10}),
              "mov rax, QWORD PTR [rbp+0x10000000]");
}

TEST(DecodeTest, RipRelative) {
    // 48 8B 05 disp32: mov rax, [rip+0x10]; rip=0, length=7.
    EXPECT_EQ(text_of({0x48, 0x8B, 0x05, 0x10, 0x00, 0x00, 0x00}, 0),
              "mov rax, QWORD PTR [rip+0x10]");
}

TEST(DecodeTest, BranchesAndCalls) {
    EXPECT_EQ(text_of({0xE9, 0x10, 0x00, 0x00, 0x00}, 0), "jmp 0x15");
    EXPECT_EQ(text_of({0xEB, 0xFE}, 0x1000), "jmp 0x1000");
    EXPECT_EQ(text_of({0x74, 0x02}, 0), "je 0x4");
    EXPECT_EQ(text_of({0x0F, 0x84, 0x08, 0x00, 0x00, 0x00}, 0), "je 0xe");
    EXPECT_EQ(text_of({0xE8, 0x05, 0x00, 0x00, 0x00}, 0), "call 0xa");
    EXPECT_EQ(text_of({0xC3}), "ret");
}

TEST(DecodeTest, PushPop) {
    EXPECT_EQ(text_of({0x50}), "push rax");
    EXPECT_EQ(text_of({0x41, 0x54}), "push r12");
    EXPECT_EQ(text_of({0x58}), "pop rax");
    EXPECT_EQ(text_of({0x68, 0x78, 0x56, 0x34, 0x12}), "push 0x12345678");
}

TEST(DecodeTest, PrefixLockAndRep) {
    // lock cmpxchg [rdi], edx
    EXPECT_EQ(text_of({0xF0, 0x0F, 0xB1, 0x17}), "lock cmpxchg DWORD PTR [rdi], edx");
    EXPECT_EQ(text_of({0xF3, 0xA4}), "rep movsb");
    EXPECT_EQ(text_of({0xF3, 0x0F, 0x1E, 0xFA}), "endbr64");
    EXPECT_EQ(text_of({0xF3, 0x90}), "pause");
}

TEST(DecodeTest, AluForms) {
    EXPECT_EQ(text_of({0x01, 0xC8}), "add eax, ecx");
    EXPECT_EQ(text_of({0x48, 0x01, 0xC8}), "add rax, rcx");
    EXPECT_EQ(text_of({0x83, 0xC0, 0x10}), "add eax, 0x10");
    EXPECT_EQ(text_of({0x81, 0xC0, 0x00, 0x00, 0x00, 0x10}), "add eax, 0x10000000");
    EXPECT_EQ(text_of({0x83, 0xC0, 0xF0}), "add eax, 0xfffffff0");
    EXPECT_EQ(text_of({0x48, 0x83, 0xEC, 0x28}), "sub rsp, 0x28");
    EXPECT_EQ(text_of({0x85, 0xC0}), "test eax, eax");
    EXPECT_EQ(text_of({0x48, 0x39, 0xC8}), "cmp rax, rcx");
    // F7 /0: test r/m, imm32
    EXPECT_EQ(text_of({0x48, 0xF7, 0xC7, 0xFF, 0xFF, 0xFF, 0x7F}), "test rdi, 0x7fffffff");
}

TEST(DecodeTest, Shifts) {
    EXPECT_EQ(text_of({0x48, 0xC1, 0xE0, 0x03}), "shl rax, 0x3");
    EXPECT_EQ(text_of({0x48, 0xD1, 0xE0}), "shl rax, 1");
    EXPECT_EQ(text_of({0x48, 0xD3, 0xE0}), "shl rax, cl");
    EXPECT_EQ(text_of({0x48, 0xC1, 0xF8, 0x3F}), "sar rax, 0x3f");
}

TEST(DecodeTest, MovsxMovzx) {
    EXPECT_EQ(text_of({0x48, 0x0F, 0xB6, 0x03}), "movzx rax, BYTE PTR [rbx]");
    EXPECT_EQ(text_of({0x0F, 0xB7, 0x03}), "movzx eax, WORD PTR [rbx]");
    EXPECT_EQ(text_of({0x48, 0x0F, 0xBE, 0x03}), "movsx rax, BYTE PTR [rbx]");
    // movsxd rax, dword [rbx] = 63 0F BE 03? No: movsxd is 63 /r.
    EXPECT_EQ(text_of({0x48, 0x63, 0x03}), "movsxd rax, DWORD PTR [rbx]");
}

TEST(DecodeTest, Lea) {
    EXPECT_EQ(text_of({0x48, 0x8D, 0x3D, 0x10, 0x00, 0x00, 0x00}), "lea rdi, [rip+0x10]");
    EXPECT_EQ(text_of({0x48, 0x8D, 0x04, 0x85, 0x00, 0x00, 0x00, 0x00}), "lea rax, [rax*4+0x0]");
}

TEST(DecodeTest, SystemInsns) {
    EXPECT_EQ(text_of({0x0F, 0x05}), "syscall");
    EXPECT_EQ(text_of({0x0F, 0x30}), "wrmsr");
    EXPECT_EQ(text_of({0x0F, 0x31}), "rdtsc");
    EXPECT_EQ(text_of({0x0F, 0x32}), "rdmsr");
    EXPECT_EQ(text_of({0x0F, 0x01, 0xF8}), "swapgs");
    // invlpg [rax]: 0F 01 /7 m.
    EXPECT_EQ(text_of({0x0F, 0x01, 0x38}), "invlpg BYTE PTR [rax]");
    EXPECT_EQ(text_of({0xF4}), "hlt");
    EXPECT_EQ(text_of({0xFA}), "cli");
    EXPECT_EQ(text_of({0xFB}), "sti");
}

TEST(DecodeTest, ControlRegs) {
    EXPECT_EQ(text_of({0x0F, 0x20, 0xC0}), "mov rax, cr0");
    EXPECT_EQ(text_of({0x0F, 0x22, 0xC0}), "mov cr0, rax");
    EXPECT_EQ(text_of({0x0F, 0x20, 0xD8}), "mov rax, cr3");
}

TEST(DecodeTest, StringAndFlags) {
    EXPECT_EQ(text_of({0xFC}), "cld");
    EXPECT_EQ(text_of({0xFD}), "std");
    EXPECT_EQ(text_of({0xAA}), "stosb");
    EXPECT_EQ(text_of({0x48, 0xAB}), "stosq");
    EXPECT_EQ(text_of({0xAF}), "scasd");
}

TEST(DecodeTest, SseSubset) {
    EXPECT_EQ(text_of({0x66, 0x0F, 0x6F, 0x06}), "movdqa xmm0, XMMWORD PTR [rsi]");
    EXPECT_EQ(text_of({0x66, 0x0F, 0x7F, 0x07}), "movdqa XMMWORD PTR [rdi], xmm0");
    EXPECT_EQ(text_of({0xF3, 0x0F, 0x6F, 0x06}), "movdqu xmm0, XMMWORD PTR [rsi]");
    EXPECT_EQ(text_of({0x66, 0x0F, 0xEF, 0xC0}), "pxor xmm0, xmm0");
    EXPECT_EQ(text_of({0x66, 0x0F, 0x70, 0xC0, 0x4E}), "pshufd xmm0, xmm0, 0x4e");
    // movq xmm, r/m64: F3 0F 7E.
    EXPECT_EQ(text_of({0xF3, 0x0F, 0x7E, 0x07}), "movq xmm0, QWORD PTR [rdi]");
    // pcmpeqq: 66 0F 38 29.
    EXPECT_EQ(text_of({0x66, 0x0F, 0x38, 0x29, 0xC3}), "pcmpeqq xmm0, xmm3");
}

TEST(DecodeTest, SetccCmovLoop) {
    EXPECT_EQ(text_of({0x0F, 0x94, 0xC0}), "sete al");
    EXPECT_EQ(text_of({0x0F, 0x95, 0xC3}), "setne bl");
    EXPECT_EQ(text_of({0x48, 0x0F, 0x44, 0xC1}), "cmove rax, rcx");
    EXPECT_EQ(text_of({0xE2, 0xFD}), "loop 0xffffffffffffffff");
}

TEST(DecodeTest, NopVariants) {
    EXPECT_EQ(text_of({0x90}), "nop");
    // multi-byte nop 0F 1F 00.
    EXPECT_EQ(text_of({0x0F, 0x1F, 0x00}), "nop DWORD PTR [rax]");
}

TEST(DecodeTest, XchgXaddCmpxchg) {
    EXPECT_EQ(text_of({0x91}), "xchg ecx, eax");
    EXPECT_EQ(text_of({0x48, 0x0F, 0xC1, 0x07}), "xadd QWORD PTR [rdi], rax");
    EXPECT_EQ(text_of({0x0F, 0xB1, 0x17}), "cmpxchg DWORD PTR [rdi], edx");
}

TEST(DecodeTest, BtFamily) {
    EXPECT_EQ(text_of({0x48, 0x0F, 0xBA, 0xE7, 0x3F}), "bts rdi, 0x3f");
    EXPECT_EQ(text_of({0x0F, 0xBA, 0xE7, 0x3F}), "bts edi, 0x3f");
    EXPECT_EQ(text_of({0x48, 0x0F, 0xA3, 0xD8}), "bt rax, rbx");
}

TEST(DecodeTest, InOut) {
    EXPECT_EQ(text_of({0xE4, 0x64}), "in al, 0x64");
    EXPECT_EQ(text_of({0xEC}), "in al, dx");
    EXPECT_EQ(text_of({0xEE}), "out dx, al");
}

TEST(DecodeTest, ErrorChannels) {
    const std::vector<uint8_t> ud2 = {0x0F, 0x0B};
    auto r = mf::decode_one(ud2);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().kind, mf::DecodeError::Kind::kUnsupportedOpcode);

    const std::vector<uint8_t> trunc = {0x48};
    r = mf::decode_one(trunc);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().kind, mf::DecodeError::Kind::kTruncated);

    // x87 fadd is outside the whitelist.
    const std::vector<uint8_t> x87 = {0xD8, 0xC1};
    r = mf::decode_one(x87);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().kind, mf::DecodeError::Kind::kUnsupportedOpcode);

    // lea with register operand is invalid.
    const std::vector<uint8_t> lea_reg = {0x48, 0x8D, 0xC0};
    r = mf::decode_one(lea_reg);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().kind, mf::DecodeError::Kind::kBadModrm);
}

TEST(DecodeTest, LengthAccounting) {
    struct Case {
        std::vector<uint8_t> bytes;
        uint8_t length;
    };
    const Case cases[] = {
        {.bytes = {0x90}, .length = 1},
        {.bytes = {0x48, 0x89, 0xC8}, .length = 3},
        {.bytes = {0x49, 0xB8, 1, 2, 3, 4, 5, 6, 7, 8}, .length = 10},
        {.bytes = {0xF3, 0x0F, 0x1E, 0xFA}, .length = 4},
        {.bytes = {0x66, 0x0F, 0x38, 0x29, 0xC3}, .length = 5},
    };
    for (const Case& c : cases) {
        const auto r = mf::decode_one(c.bytes);
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(r->length, c.length);
    }
}

TEST(DecodeTest, RegressionRexXIndex) {
    // Caught by the objdump differential: REX.X + SIB index 4 is r12.
    EXPECT_EQ(text_of({0x4A, 0x89, 0x04, 0xE3}), "mov QWORD PTR [rbx+r12*8], rax");
}

TEST(DecodeTest, RegressionCrRegField) {
    // Caught by the differential: the CR number lives in the reg field.
    EXPECT_EQ(text_of({0x0F, 0x20, 0xE0}), "mov rax, cr4");
    EXPECT_EQ(text_of({0x0F, 0x20, 0xD8}), "mov rax, cr3");
}

TEST(DecodeTest, RegressionMovqDirection) {
    // 66 0F D6 is the store direction; 66 REX.W 0F 7E is movq r64, xmm.
    EXPECT_EQ(text_of({0x66, 0x0F, 0xD6, 0x07}), "movq QWORD PTR [rdi], xmm0");
    EXPECT_EQ(text_of({0x66, 0x48, 0x0F, 0x7E, 0xC2}), "movq rdx, xmm0");
}

TEST(DecodeTest, RegressionFFOperandSizes) {
    // FF /2 call r/m64 defaults to 64-bit; FF /0 inc defaults to 32-bit.
    EXPECT_EQ(text_of({0xFF, 0x10}), "call QWORD PTR [rax]");
    EXPECT_EQ(text_of({0xFF, 0xD0}), "call rax");
    EXPECT_EQ(text_of({0xFF, 0x00}), "inc DWORD PTR [rax]");
    EXPECT_EQ(text_of({0xFE, 0x00}), "inc BYTE PTR [rax]");
}

TEST(DecodeTest, RegressionReservedSregRejected) {
    const std::vector<uint8_t> bad_sreg = {0x8E, 0xF0}; // reg=6: no segment
    auto r = mf::decode_one(bad_sreg);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().kind, mf::DecodeError::Kind::kUnsupportedOpcode);
}

TEST(DecodeTest, PrefixSemanticsDirect) {
    // Segment overrides land in DecodedInsn.seg (asserted directly, not
    // through rendering).
    const std::vector<uint8_t> fs_abs = {0x64, 0x8B, 0x04, 0x25, 0x08, 0x00, 0x00, 0x00};
    auto r = mf::decode_one(fs_abs);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->seg, mf::Seg::kFs);
    // The rep prefix survives only on string families.
    const std::vector<uint8_t> rep_str = {0xF3, 0xA4};
    r = mf::decode_one(rep_str);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->rep, mf::Rep::kRep);
    const std::vector<uint8_t> rep_plain = {0xF3, 0x31, 0xC0}; // xor eax, eax
    r = mf::decode_one(rep_plain);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->rep, mf::Rep::kNone);
}

TEST(DecodeTest, RexAfterLegacyPrefixWins) {
    // binutils (our differential authority) drops the 66 and keeps the
    // REX bits; SDM 2.2.1.2 words the precedence the other way. Recorded
    // divergence: real toolchains never emit REX before 66.
    EXPECT_EQ(text_of({0x48, 0x66, 0x89, 0xC8}), "mov rax, rcx");
}

TEST(DecodeTest, BadAddrSizePrefixRejected) {
    const std::vector<uint8_t> p67 = {0x67, 0x89, 0xC8};
    auto r = mf::decode_one(p67);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().kind, mf::DecodeError::Kind::kBadPrefix);
}

TEST(DecodeTest, ReservedCrDrRejected) {
    // CR1 and DR4/DR5 are reserved: rejected, not aliased.
    const std::vector<uint8_t> cr1 = {0x0F, 0x20, 0xC8}; // reg=1 -> CR1
    auto r = mf::decode_one(cr1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().kind, mf::DecodeError::Kind::kUnsupportedOpcode);
    const std::vector<uint8_t> dr4 = {0x0F, 0x21, 0xE0}; // reg=4 -> DR4
    r = mf::decode_one(dr4);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().kind, mf::DecodeError::Kind::kUnsupportedOpcode);
}

TEST(DecodeTest, PackedShiftImmRequiresRegisterForm) {
    // 66 0F 71 with a memory ModRM is undefined on hardware.
    const std::vector<uint8_t> mem_form = {0x66, 0x0F, 0x71, 0x00};
    auto r = mf::decode_one(mem_form);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().kind, mf::DecodeError::Kind::kUnsupportedOpcode);
    EXPECT_EQ(text_of({0x66, 0x0F, 0x71, 0xF0, 0x03}), "psllw xmm0, 0x3");
    EXPECT_EQ(text_of({0x66, 0x0F, 0x71, 0xD1, 0x02}), "psrlw xmm1, 0x2");
    // /0 and /1 in the 0F 71-73 imm groups are undefined on hardware.
    const std::vector<uint8_t> bad_reg = {0x66, 0x0F, 0x71, 0xC0, 0x03};
    const auto r2 = mf::decode_one(bad_reg);
    ASSERT_FALSE(r2.has_value());
}

TEST(DecodeTest, SysretWidthForms) {
    // binutils: bare 0F 07 renders sysretd; REX.W promotes to sysretq.
    EXPECT_EQ(text_of({0x0F, 0x07}), "sysretd");
    EXPECT_EQ(text_of({0x48, 0x0F, 0x07}), "sysretq");
}

} // namespace
