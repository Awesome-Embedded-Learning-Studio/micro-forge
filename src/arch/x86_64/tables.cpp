/**
 * @file tables.cpp
 * @brief Opcode tables for the x86-64 decoder (spec section 5.2 set).
 * @version 0.1.0
 */

#include "arch/x86_64/internal.hpp"

#include "micro_forge/kernel/le_access.hpp"

namespace micro_forge::arch::x86_64::detail {
namespace {

constexpr std::array<Ent, 256> build_primary() {
    std::array<Ent, 256> t{};
    const struct AluFamily {
        uint8_t base;
        Mnemonic mn;
    } alu[] = {
        {.base = 0x00, .mn = Mnemonic::kAdd}, {.base = 0x08, .mn = Mnemonic::kOr},
        {.base = 0x10, .mn = Mnemonic::kAdc}, {.base = 0x18, .mn = Mnemonic::kSbb},
        {.base = 0x20, .mn = Mnemonic::kAnd}, {.base = 0x28, .mn = Mnemonic::kSub},
        {.base = 0x30, .mn = Mnemonic::kXor}, {.base = 0x38, .mn = Mnemonic::kCmp},
    };
    for (const AluFamily& f : alu) {
        t[f.base + 0] = make_ent(f.mn, Slot::kRm8, Slot::kReg8, Slot::kNone, SizeR::kB);
        t[f.base + 1] = make_ent(f.mn, Slot::kRm, Slot::kReg, Slot::kNone, SizeR::kD);
        t[f.base + 2] = make_ent(f.mn, Slot::kReg8, Slot::kRm8, Slot::kNone, SizeR::kB);
        t[f.base + 3] = make_ent(f.mn, Slot::kReg, Slot::kRm, Slot::kNone, SizeR::kD);
        t[f.base + 4] =
            make_ent(f.mn, Slot::kAcc8, Slot::kNone, Slot::kNone, SizeR::kB, ImmK::k8, 1);
        t[f.base + 5] =
            make_ent(f.mn, Slot::kAcc, Slot::kNone, Slot::kNone, SizeR::kD, ImmK::kSz4, 1);
    }
    for (uint8_t r = 0; r < 8; ++r) {
        t[0x50 + r] = make_ent(Mnemonic::kPush, Slot::kOpr, Slot::kNone, Slot::kNone, SizeR::kP);
        t[0x58 + r] = make_ent(Mnemonic::kPop, Slot::kOpr, Slot::kNone, Slot::kNone, SizeR::kP);
    }
    t[0x63] = make_ent(Mnemonic::kMovsxd, Slot::kReg, Slot::kRm32, Slot::kNone, SizeR::kD);
    t[0x68] =
        make_ent(Mnemonic::kPush, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kP, ImmK::kSz4, 0);
    t[0x6A] =
        make_ent(Mnemonic::kPush, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kP, ImmK::k8S, 0);
    t[0x69] =
        make_ent(Mnemonic::kImul, Slot::kReg, Slot::kRm, Slot::kNone, SizeR::kD, ImmK::kSz4, 2);
    t[0x6B] =
        make_ent(Mnemonic::kImul, Slot::kReg, Slot::kRm, Slot::kNone, SizeR::kD, ImmK::k8S, 2);
    for (uint8_t cc = 0; cc < 16; ++cc) {
        t[0x70 + cc] = make_ent(Mnemonic::kJcc, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kD,
                                ImmK::k8S, 0);
    }
    t[0x80] = make_ent(Mnemonic::kNop, Slot::kRm8, Slot::kNone, Slot::kNone, SizeR::kB, ImmK::k8, 1,
                       kGrp1);
    t[0x81] = make_ent(Mnemonic::kNop, Slot::kRm, Slot::kNone, Slot::kNone, SizeR::kD, ImmK::kSz4,
                       1, kGrp1);
    t[0x83] = make_ent(Mnemonic::kNop, Slot::kRm, Slot::kNone, Slot::kNone, SizeR::kD, ImmK::k8S, 1,
                       kGrp1);
    t[0x84] = make_ent(Mnemonic::kTest, Slot::kRm8, Slot::kReg8, Slot::kNone, SizeR::kB);
    t[0x85] = make_ent(Mnemonic::kTest, Slot::kRm, Slot::kReg, Slot::kNone, SizeR::kD);
    t[0x86] = make_ent(Mnemonic::kXchg, Slot::kRm8, Slot::kReg8, Slot::kNone, SizeR::kB);
    t[0x87] = make_ent(Mnemonic::kXchg, Slot::kRm, Slot::kReg, Slot::kNone, SizeR::kD);
    t[0x88] = make_ent(Mnemonic::kMov, Slot::kRm8, Slot::kReg8, Slot::kNone, SizeR::kB);
    t[0x89] = make_ent(Mnemonic::kMov, Slot::kRm, Slot::kReg, Slot::kNone, SizeR::kD);
    t[0x8A] = make_ent(Mnemonic::kMov, Slot::kReg8, Slot::kRm8, Slot::kNone, SizeR::kB);
    t[0x8B] = make_ent(Mnemonic::kMov, Slot::kReg, Slot::kRm, Slot::kNone, SizeR::kD);
    t[0x8C] = make_ent(Mnemonic::kMov, Slot::kRm, Slot::kSreg, Slot::kNone, SizeR::kD);
    t[0x8D] = make_ent(Mnemonic::kLea, Slot::kReg, Slot::kMem, Slot::kNone, SizeR::kD);
    t[0x8E] = make_ent(Mnemonic::kMov, Slot::kSreg, Slot::kRm, Slot::kNone, SizeR::kD);
    t[0x8F] = make_ent(Mnemonic::kPop, Slot::kRm, Slot::kNone, Slot::kNone, SizeR::kP);
    t[0x90] = make_ent(Mnemonic::kNop);
    for (uint8_t r = 1; r < 8; ++r) {
        t[0x90 + r] = make_ent(Mnemonic::kXchg, Slot::kOpr, Slot::kAcc, Slot::kNone, SizeR::kD);
    }
    t[0x98] = make_ent(Mnemonic::kCwde);
    t[0x99] = make_ent(Mnemonic::kCdq);
    t[0xA0] =
        make_ent(Mnemonic::kMov, Slot::kAcc8, Slot::kMem, Slot::kNone, SizeR::kB, ImmK::k64, 1);
    t[0xA1] =
        make_ent(Mnemonic::kMov, Slot::kAcc, Slot::kMem, Slot::kNone, SizeR::kD, ImmK::k64, 1);
    t[0xA2] =
        make_ent(Mnemonic::kMov, Slot::kMem, Slot::kAcc8, Slot::kNone, SizeR::kB, ImmK::k64, 0);
    t[0xA3] =
        make_ent(Mnemonic::kMov, Slot::kMem, Slot::kAcc, Slot::kNone, SizeR::kD, ImmK::k64, 0);
    t[0xA4] = make_ent(Mnemonic::kMovsb);
    t[0xA5] = make_ent(Mnemonic::kMovsd);
    t[0xA6] = make_ent(Mnemonic::kCmpsb);
    t[0xA7] = make_ent(Mnemonic::kCmpsq);
    t[0xA8] =
        make_ent(Mnemonic::kTest, Slot::kAcc8, Slot::kNone, Slot::kNone, SizeR::kB, ImmK::k8, 1);
    t[0xA9] =
        make_ent(Mnemonic::kTest, Slot::kAcc, Slot::kNone, Slot::kNone, SizeR::kD, ImmK::kSz4, 1);
    t[0xAA] = make_ent(Mnemonic::kStosb);
    t[0xAB] = make_ent(Mnemonic::kStosd);
    t[0xAC] = make_ent(Mnemonic::kLodsb);
    t[0xAD] = make_ent(Mnemonic::kLodsd);
    t[0xAE] = make_ent(Mnemonic::kScasb);
    t[0xAF] = make_ent(Mnemonic::kScasd);
    for (uint8_t r = 0; r < 8; ++r) {
        t[0xB0 + r] =
            make_ent(Mnemonic::kMov, Slot::kOpr8, Slot::kNone, Slot::kNone, SizeR::kB, ImmK::k8, 1);
        t[0xB8 + r] = make_ent(Mnemonic::kMov, Slot::kOpr, Slot::kNone, Slot::kNone, SizeR::kD,
                               ImmK::kSz4, 1);
    }
    t[0xC0] = make_ent(Mnemonic::kNop, Slot::kRm8, Slot::kNone, Slot::kNone, SizeR::kB, ImmK::k8, 1,
                       kGrp2);
    t[0xC1] = make_ent(Mnemonic::kNop, Slot::kRm, Slot::kNone, Slot::kNone, SizeR::kD, ImmK::k8, 1,
                       kGrp2);
    t[0xC2] =
        make_ent(Mnemonic::kRet, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kQ, ImmK::k16, 0);
    t[0xC3] = make_ent(Mnemonic::kRet);
    t[0xC6] =
        make_ent(Mnemonic::kMov, Slot::kRm8, Slot::kNone, Slot::kNone, SizeR::kB, ImmK::k8, 1);
    t[0xC7] =
        make_ent(Mnemonic::kMov, Slot::kRm, Slot::kNone, Slot::kNone, SizeR::kD, ImmK::kSz4, 1);
    t[0xC9] = make_ent(Mnemonic::kLeave);
    t[0xCF] = make_ent(Mnemonic::kIretq, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kQ);
    t[0xD0] = make_ent(Mnemonic::kNop, Slot::kRm8, Slot::kOne, Slot::kNone, SizeR::kB, ImmK::k0,
                       kNoImm, kGrp2);
    t[0xD1] = make_ent(Mnemonic::kNop, Slot::kRm, Slot::kOne, Slot::kNone, SizeR::kD, ImmK::k0,
                       kNoImm, kGrp2);
    t[0xD2] = make_ent(Mnemonic::kNop, Slot::kRm8, Slot::kCl, Slot::kNone, SizeR::kB, ImmK::k0,
                       kNoImm, kGrp2);
    t[0xD3] = make_ent(Mnemonic::kNop, Slot::kRm, Slot::kCl, Slot::kNone, SizeR::kD, ImmK::k0,
                       kNoImm, kGrp2);
    t[0xE0] =
        make_ent(Mnemonic::kLoopne, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kQ, ImmK::k8S, 0);
    t[0xE1] =
        make_ent(Mnemonic::kLoope, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kQ, ImmK::k8S, 0);
    t[0xE2] =
        make_ent(Mnemonic::kLoop, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kQ, ImmK::k8S, 0);
    t[0xE3] =
        make_ent(Mnemonic::kJrcxz, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kQ, ImmK::k8S, 0);
    t[0xE4] =
        make_ent(Mnemonic::kIn, Slot::kAcc8, Slot::kNone, Slot::kNone, SizeR::kB, ImmK::k8, 1);
    t[0xE5] = make_ent(Mnemonic::kIn, Slot::kAcc, Slot::kNone, Slot::kNone, SizeR::kD, ImmK::k8, 1);
    t[0xE6] =
        make_ent(Mnemonic::kOut, Slot::kNone, Slot::kAcc8, Slot::kNone, SizeR::kB, ImmK::k8, 0);
    t[0xE7] =
        make_ent(Mnemonic::kOut, Slot::kNone, Slot::kAcc, Slot::kNone, SizeR::kD, ImmK::k8, 0);
    t[0xEC] = make_ent(Mnemonic::kIn, Slot::kAcc8, Slot::kDx, Slot::kNone, SizeR::kB);
    t[0xED] = make_ent(Mnemonic::kIn, Slot::kAcc, Slot::kDx, Slot::kNone, SizeR::kD);
    t[0xEE] = make_ent(Mnemonic::kOut, Slot::kDx, Slot::kAcc8, Slot::kNone, SizeR::kB);
    t[0xEF] = make_ent(Mnemonic::kOut, Slot::kDx, Slot::kAcc, Slot::kNone, SizeR::kD);
    t[0xE8] =
        make_ent(Mnemonic::kCall, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kQ, ImmK::k32S, 0);
    t[0xE9] =
        make_ent(Mnemonic::kJmp, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kQ, ImmK::k32S, 0);
    t[0xEB] =
        make_ent(Mnemonic::kJmp, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kQ, ImmK::k8S, 0);
    t[0xF4] = make_ent(Mnemonic::kHlt);
    t[0xF6] = make_ent(Mnemonic::kNop, Slot::kRm8, Slot::kNone, Slot::kNone, SizeR::kB, ImmK::k0,
                       kNoImm, kGrp3);
    t[0xF7] = make_ent(Mnemonic::kNop, Slot::kRm, Slot::kNone, Slot::kNone, SizeR::kD, ImmK::k0,
                       kNoImm, kGrp3);
    t[0xFA] = make_ent(Mnemonic::kCli);
    t[0xFB] = make_ent(Mnemonic::kSti);
    t[0xFC] = make_ent(Mnemonic::kCld);
    t[0xFD] = make_ent(Mnemonic::kStd);
    t[0xFE] = make_ent(Mnemonic::kNop, Slot::kRm8, Slot::kNone, Slot::kNone, SizeR::kB, ImmK::k0,
                       kNoImm, kGrp4);
    t[0xFF] = make_ent(Mnemonic::kNop, Slot::kRm, Slot::kNone, Slot::kNone, SizeR::kD, ImmK::k0,
                       kNoImm, kGrp5);
    return t;
}

// ---------------------------------------------------------------------------
// 0F escape tables. Index by mandatory-prefix modifier: 0 none, 1 = 66,
// 2 = F2, 3 = F3. Separate 0F38 table exists for 3-byte opcodes.
// ---------------------------------------------------------------------------

constexpr std::array<Ent, 256> build_esc0() {
    std::array<Ent, 256> t{};
    t[0x05] = make_ent(Mnemonic::kSyscall);
    t[0x07] = make_ent(Mnemonic::kSysret);
    t[0x01] = make_ent(Mnemonic::kNop, Slot::kMem, Slot::kNone, Slot::kNone, SizeR::kQ, ImmK::k0,
                       kNoImm, kGrp6);
    t[0x10] = make_ent(Mnemonic::kMovups, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0x11] = make_ent(Mnemonic::kMovups, Slot::kXRm, Slot::kXReg, Slot::kNone, SizeR::kV);
    for (uint8_t op = 0x18; op <= 0x1F; ++op) {
        t[op] = make_ent(Mnemonic::kNop, Slot::kRm, Slot::kNone, Slot::kNone, SizeR::kD);
    }
    t[0x20] = make_ent(Mnemonic::kMov, Slot::kRm, Slot::kCr, Slot::kNone, SizeR::kQ);
    t[0x21] = make_ent(Mnemonic::kMov, Slot::kRm, Slot::kDr, Slot::kNone, SizeR::kQ);
    t[0x22] = make_ent(Mnemonic::kMov, Slot::kCr, Slot::kRm, Slot::kNone, SizeR::kQ);
    t[0x23] = make_ent(Mnemonic::kMov, Slot::kDr, Slot::kRm, Slot::kNone, SizeR::kQ);
    t[0x30] = make_ent(Mnemonic::kWrmsr);
    t[0x31] = make_ent(Mnemonic::kRdtsc);
    t[0x32] = make_ent(Mnemonic::kRdmsr);
    t[0x28] = make_ent(Mnemonic::kMovaps, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0x29] = make_ent(Mnemonic::kMovaps, Slot::kXRm, Slot::kXReg, Slot::kNone, SizeR::kV);
    for (uint8_t cc = 0; cc < 16; ++cc) {
        t[0x40 + cc] = make_ent(Mnemonic::kCmovcc, Slot::kReg, Slot::kRm, Slot::kNone, SizeR::kD);
    }
    for (uint8_t cc = 0; cc < 16; ++cc) {
        t[0x80 + cc] = make_ent(Mnemonic::kJcc, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kD,
                                ImmK::k32S, 0);
    }
    for (uint8_t cc = 0; cc < 16; ++cc) {
        t[0x90 + cc] = make_ent(Mnemonic::kSetcc, Slot::kRm8, Slot::kNone, Slot::kNone, SizeR::kB);
    }
    t[0xA0] = make_ent(Mnemonic::kPush, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kQ);
    t[0xA1] = make_ent(Mnemonic::kPop, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kQ);
    t[0xA2] = make_ent(Mnemonic::kCpuid);
    t[0xA3] = make_ent(Mnemonic::kBt, Slot::kRm, Slot::kReg, Slot::kNone, SizeR::kD);
    t[0xA4] = make_ent(Mnemonic::kShld, Slot::kRm, Slot::kReg, Slot::kNone, SizeR::kD, ImmK::k8, 2);
    t[0xA5] = make_ent(Mnemonic::kShld, Slot::kRm, Slot::kReg, Slot::kCl, SizeR::kD);
    t[0xA8] = make_ent(Mnemonic::kPush, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kQ);
    t[0xA9] = make_ent(Mnemonic::kPop, Slot::kNone, Slot::kNone, Slot::kNone, SizeR::kQ);
    t[0xAB] = make_ent(Mnemonic::kBts, Slot::kRm, Slot::kReg, Slot::kNone, SizeR::kD);
    t[0xAC] = make_ent(Mnemonic::kShrd, Slot::kRm, Slot::kReg, Slot::kNone, SizeR::kD, ImmK::k8, 2);
    t[0xAD] = make_ent(Mnemonic::kShrd, Slot::kRm, Slot::kReg, Slot::kCl, SizeR::kD);
    t[0xAF] = make_ent(Mnemonic::kImul, Slot::kReg, Slot::kRm, Slot::kNone, SizeR::kD);
    t[0xB0] = make_ent(Mnemonic::kCmpxchg, Slot::kRm8, Slot::kReg8, Slot::kNone, SizeR::kB);
    t[0xB1] = make_ent(Mnemonic::kCmpxchg, Slot::kRm, Slot::kReg, Slot::kNone, SizeR::kD);
    t[0xB3] = make_ent(Mnemonic::kBtr, Slot::kRm, Slot::kReg, Slot::kNone, SizeR::kD);
    t[0xB6] = make_ent(Mnemonic::kMovzx, Slot::kReg, Slot::kRm8, Slot::kNone, SizeR::kD);
    t[0xB7] = make_ent(Mnemonic::kMovzx, Slot::kReg, Slot::kRm16, Slot::kNone, SizeR::kD);
    t[0xBA] = make_ent(Mnemonic::kNop, Slot::kRm, Slot::kNone, Slot::kNone, SizeR::kD, ImmK::k8, 1,
                       kGrp8);
    t[0xBB] = make_ent(Mnemonic::kBtc, Slot::kRm, Slot::kReg, Slot::kNone, SizeR::kD);
    t[0xBC] = make_ent(Mnemonic::kBsf, Slot::kReg, Slot::kRm, Slot::kNone, SizeR::kD);
    t[0xBD] = make_ent(Mnemonic::kBsr, Slot::kReg, Slot::kRm, Slot::kNone, SizeR::kD);
    t[0xBE] = make_ent(Mnemonic::kMovsx, Slot::kReg, Slot::kRm8, Slot::kNone, SizeR::kD);
    t[0xBF] = make_ent(Mnemonic::kMovsx, Slot::kReg, Slot::kRm16, Slot::kNone, SizeR::kD);
    t[0xC0] = make_ent(Mnemonic::kXadd, Slot::kRm8, Slot::kReg8, Slot::kNone, SizeR::kB);
    t[0xC1] = make_ent(Mnemonic::kXadd, Slot::kRm, Slot::kReg, Slot::kNone, SizeR::kD);
    t[0xC7] = make_ent(Mnemonic::kNop, Slot::kReg, Slot::kNone, Slot::kNone, SizeR::kD, ImmK::k0,
                       kNoImm, kGrp9);
    for (uint8_t r = 0; r < 8; ++r) {
        t[0xC8 + r] = make_ent(Mnemonic::kBswap, Slot::kOpr, Slot::kNone, Slot::kNone, SizeR::kD);
    }
    t[0xAE] = make_ent(Mnemonic::kNop, Slot::kRm, Slot::kNone, Slot::kNone, SizeR::kV, ImmK::k0,
                       kNoImm, kGrpAe);
    return t;
}

constexpr std::array<Ent, 256> build_esc66() {
    std::array<Ent, 256> t{};
    t[0x60] = make_ent(Mnemonic::kPunpcklbw, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0x61] = make_ent(Mnemonic::kPunpcklwd, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0x62] = make_ent(Mnemonic::kPunpckldq, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0x67] = make_ent(Mnemonic::kPackuswb, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0x68] = make_ent(Mnemonic::kPunpckhbw, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0x69] = make_ent(Mnemonic::kPunpckhwd, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0x6A] = make_ent(Mnemonic::kPunpckhdq, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0x6C] = make_ent(Mnemonic::kPunpcklqdq, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0x6D] = make_ent(Mnemonic::kPunpckhqdq, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0x6E] = make_ent(Mnemonic::kMovd_xmm, Slot::kXReg, Slot::kRm32S, Slot::kNone, SizeR::kV);
    t[0x6F] = make_ent(Mnemonic::kMovdqa, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0x70] =
        make_ent(Mnemonic::kPshufd, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV, ImmK::k8, 2);
    t[0x71] = make_ent(Mnemonic::kNop, Slot::kXReg, Slot::kNone, Slot::kNone, SizeR::kV, ImmK::k8,
                       1, kGrpPs);
    t[0x72] = make_ent(Mnemonic::kNop, Slot::kXReg, Slot::kNone, Slot::kNone, SizeR::kV, ImmK::k8,
                       1, kGrpPs);
    t[0x73] = make_ent(Mnemonic::kNop, Slot::kXReg, Slot::kNone, Slot::kNone, SizeR::kV, ImmK::k8,
                       1, kGrpPs);
    t[0x74] = make_ent(Mnemonic::kPcmpeqb, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0x76] = make_ent(Mnemonic::kPcmpeqd, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0x7E] = make_ent(Mnemonic::kMovd_xmm, Slot::kXRm32, Slot::kXReg, Slot::kNone, SizeR::kV);
    t[0x7F] = make_ent(Mnemonic::kMovdqa, Slot::kXRm, Slot::kXReg, Slot::kNone, SizeR::kV);
    t[0xD1] = make_ent(Mnemonic::kPsrlw, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0xD2] = make_ent(Mnemonic::kPsrld, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0xD3] = make_ent(Mnemonic::kPsrlq, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0xD4] = make_ent(Mnemonic::kPaddq, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0xD6] = make_ent(Mnemonic::kMovq_xmm, Slot::kXRm64, Slot::kXReg, Slot::kNone,
                       SizeR::kV); // 66 0F D6: movq r/m64, xmm (store direction)
    t[0xDB] = make_ent(Mnemonic::kPand, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0xDF] = make_ent(Mnemonic::kPandn, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0xEB] = make_ent(Mnemonic::kPor, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0xEF] = make_ent(Mnemonic::kPxor, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0xF1] = make_ent(Mnemonic::kPsllw, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0xF2] = make_ent(Mnemonic::kPslld, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0xF3] = make_ent(Mnemonic::kPsllq, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0xFA] = make_ent(Mnemonic::kPsubd, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0xFE] = make_ent(Mnemonic::kPaddd, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    return t;
}

constexpr std::array<Ent, 256> build_esc_f3() {
    std::array<Ent, 256> t{};
    t[0x6F] = make_ent(Mnemonic::kMovdqu, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    t[0x7E] = make_ent(Mnemonic::kMovq_xmm, Slot::kXReg, Slot::kXRm64, Slot::kNone, SizeR::kV);
    t[0x7F] = make_ent(Mnemonic::kMovdqu, Slot::kXRm, Slot::kXReg, Slot::kNone, SizeR::kV);
    t[0xBC] = make_ent(Mnemonic::kTzcnt, Slot::kReg, Slot::kRm, Slot::kNone, SizeR::kD);
    t[0xBD] = make_ent(Mnemonic::kLzcnt, Slot::kReg, Slot::kRm, Slot::kNone, SizeR::kD);
    // F3 0F 1E FA = endbr64; strictly validated in the decoder body.
    // The kRm slot pulls in the ModRM byte for validation.
    t[0x1E] = make_ent(Mnemonic::kEndbr64, Slot::kRm, Slot::kNone, Slot::kNone, SizeR::kQ);
    return t;
}

/// @brief 0F38 three-byte opcode table (mandatory 66 prefix forms).
constexpr std::array<Ent, 256> build_esc38_66() {
    std::array<Ent, 256> t{};
    t[0x29] = make_ent(Mnemonic::kPcmpeqq, Slot::kXReg, Slot::kXRm, Slot::kNone, SizeR::kV);
    return t;
}

} // namespace

// std::array aggregate initialization is constexpr and cannot throw; the
// checker cannot see through the aggregate here.
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
const std::array<Ent, 256> kPrimary = build_primary();
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
const EscTables kEsc = {
    .none = build_esc0(),
    .p66 = build_esc66(),
    .p2 = {}, // NOLINT(readability-trailing-comma)
    .p3 = build_esc_f3(),
};
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
const std::array<Ent, 256> kEsc38 = build_esc38_66();

Mnemonic grp1_mn(uint8_t reg) {
    constexpr std::array<Mnemonic, 8> m = {
        Mnemonic::kAdd, Mnemonic::kOr,  Mnemonic::kAdc, Mnemonic::kSbb,
        Mnemonic::kAnd, Mnemonic::kSub, Mnemonic::kXor, Mnemonic::kCmp,
    };
    return m[reg];
}

Mnemonic grp2_mn(uint8_t reg) {
    switch (reg) {
    case 0:
        return Mnemonic::kRol;
    case 1:
        return Mnemonic::kRor;
    case 2:
        return Mnemonic::kRcl;
    case 3:
        return Mnemonic::kRcr;
    case 4:
        return Mnemonic::kShl;
    case 5:
        return Mnemonic::kShr;
    case 7:
        return Mnemonic::kSar;
    default:
        return Mnemonic::kNop;
    }
}

Mnemonic grp3_mn(uint8_t reg) {
    switch (reg) {
    case 2:
        return Mnemonic::kNot;
    case 3:
        return Mnemonic::kNeg;
    case 4:
        return Mnemonic::kMul;
    case 5:
        return Mnemonic::kImul;
    case 6:
        return Mnemonic::kDiv;
    case 7:
        return Mnemonic::kIdiv;
    default:
        return Mnemonic::kNop;
    }
}

Mnemonic grp5_mn(uint8_t reg) {
    switch (reg) {
    case 0:
        return Mnemonic::kInc;
    case 1:
        return Mnemonic::kDec;
    case 2:
        return Mnemonic::kCall;
    case 4:
        return Mnemonic::kJmp;
    case 6:
        return Mnemonic::kPush;
    default:
        return Mnemonic::kNop;
    }
}

Mnemonic grp4_mn(uint8_t reg) {
    switch (reg) {
    case 0:
        return Mnemonic::kInc;
    case 1:
        return Mnemonic::kDec;
    default:
        return Mnemonic::kNop;
    }
}

/// Resolves a ModRM r/m operand (register or memory with SIB/disp) into
/// @p dst, consuming stream bytes. Returns false when the stream ends
/// mid-operand (the caller reports kTruncated).
[[nodiscard]] bool resolve_rm(Stream& st, uint8_t modrm, uint8_t opsize, const Pfx& p, Slot s,
                              Operand& dst) {
    const auto mod = static_cast<uint8_t>(kernel::bit::extract(modrm, kModrmMod));
    const auto rm = static_cast<uint8_t>(kernel::bit::extract(modrm, kModrmRm));
    dst.size = slot_width(s, opsize);
    if (mod == 3) {
        dst.kind = Operand::Kind::kReg;
        if (s == Slot::kXRm || s == Slot::kXRm64 || s == Slot::kXRm32) {
            dst.reg = xmm_at(rm, p.rex_b);
        } else if (s == Slot::kCr) {
            dst.reg = cr_at(rm);
        } else if (s == Slot::kDr) {
            dst.reg = dr_at(rm);
        } else {
            dst.reg = gpr_at(rm, p.rex_b);
        }
        return true;
    }
    dst.kind = Operand::Kind::kMem;
    bool want_disp32 = false;
    if (rm == 4) {
        if (!st.has(1)) {
            return false;
        }
        const uint8_t sib = st.next();
        dst.scale = static_cast<uint8_t>(1U << kernel::bit::extract(sib, kSibScale));
        const auto idx = static_cast<uint8_t>(kernel::bit::extract(sib, kSibIndex));
        const auto base = static_cast<uint8_t>(kernel::bit::extract(sib, kSibBase));
        if (idx != 4 || p.rex_x != 0) {
            dst.has_index = true;
            dst.index = gpr_at(idx, p.rex_x);
        } else {
            dst.scale = 1;
        }
        if (mod == 0 && base == 5) {
            want_disp32 = true;
        } else {
            dst.has_base = true;
            dst.base = gpr_at(base, p.rex_b);
        }
    } else if (mod == 0 && rm == 5) {
        dst.rip_relative = true;
        want_disp32 = true;
    } else {
        dst.has_base = true;
        dst.base = gpr_at(rm, p.rex_b);
    }
    if (mod == 1) {
        if (!st.has(1)) {
            return false;
        }
        const unsigned d8 = st.next();
        dst.disp = (d8 < 0x80U) ? static_cast<int64_t>(d8) : -static_cast<int64_t>(0x100U - d8);
        dst.has_disp = true;
    } else if (mod == 2 || want_disp32) {
        if (!st.has(4)) {
            return false;
        }
        dst.disp = static_cast<int32_t>(kernel::get_le32(st.bytes.data() + st.pos));
        st.pos += 4;
        dst.has_disp = true;
    }
    return true;
}

} // namespace micro_forge::arch::x86_64::detail
