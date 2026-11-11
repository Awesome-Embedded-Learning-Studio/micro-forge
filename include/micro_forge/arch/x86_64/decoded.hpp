/**
 * @file decoded.hpp
 * @brief Decoded-instruction IR shared by the decoder, tests, and (batch 3)
 *        the executor.
 * @version 0.1.0
 */

#pragma once

#include <cstdint>

#include "micro_forge/arch/x86_64/decode_error.hpp"

namespace micro_forge::arch::x86_64 {

/// @brief Condition codes for jcc/setcc/cmovcc, hardware-encoded 0-15.
enum class Cc : uint8_t {
    kO = 0,
    kNo,
    kB,
    kAe,
    kE,
    kNe,
    kBe,
    kA,
    kS,
    kNs,
    kP,
    kNp,
    kL,
    kGe,
    kLe,
    kG,
};

/// @brief Rep prefix state (also covers repe/repne string semantics).
enum class Rep : uint8_t { kNone, kRep, kRepne };

/// @brief Register bank, hardware-encoded.
/**
 * @brief Unified register bank: GPR, XMM, CR, DR, segment registers.
 */
enum class Reg : uint8_t {
    // General-purpose registers, encoding 0-15.
    kRax = 0,
    kRcx = 1,
    kRdx = 2,
    kRbx = 3,
    kRsp = 4,
    kRbp = 5,
    kRsi = 6,
    kRdi = 7,
    kR8 = 8,
    kR9 = 9,
    kR10 = 10,
    kR11 = 11,
    kR12 = 12,
    kR13 = 13,
    kR14 = 14,
    kR15 = 15,
    // SIMD registers, encoding 0-15.
    kXmm0 = 16,
    kXmm1 = 17,
    kXmm2 = 18,
    kXmm3 = 19,
    kXmm4 = 20,
    kXmm5 = 21,
    kXmm6 = 22,
    kXmm7 = 23,
    kXmm8 = 24,
    kXmm9 = 25,
    kXmm10 = 26,
    kXmm11 = 27,
    kXmm12 = 28,
    kXmm13 = 29,
    kXmm14 = 30,
    kXmm15 = 31,
    // Control/debug registers (subset from the v0 whitelist).
    kCr0 = 32,
    kCr2 = 33,
    kCr3 = 34,
    kCr4 = 35,
    kDr0 = 36,
    kDr1 = 37,
    kDr2 = 38,
    kDr3 = 39,
    kDr6 = 40,
    kDr7 = 41,
    // Segment registers.
    kEs = 42,
    kCs = 43,
    kSs = 44,
    kDs = 45,
    kFs = 46,
    kGs = 47,
};

/// @brief Mnemonic families of the v0 instruction set (spec section 5.2).
enum class Mnemonic : uint8_t {
    // Data movement.
    kMov,
    kMovsx,
    kMovzx,
    kMovsxd,
    kXchg,
    kLea,
    kPush,
    kPop,
    kCmovcc,
    kMovabs,
    // Arithmetic.
    kAdd,
    kAdc,
    kSub,
    kSbb,
    kInc,
    kDec,
    kNeg,
    kCmp,
    kMul,
    kImul,
    kDiv,
    kIdiv,
    // Logic.
    kAnd,
    kOr,
    kXor,
    kNot,
    kTest,
    // Shifts and rotates.
    kShl,
    kShr,
    kSar,
    kRol,
    kRor,
    kRcl,
    kRcr,
    kShld,
    kShrd,
    // Bit operations and atomics.
    kBt,
    kBts,
    kBtr,
    kBtc,
    kBsf,
    kBsr,
    kXadd,
    kCmpxchg,
    // Control flow.
    kJmp,
    kJcc,
    kCall,
    kRet,
    kLeave,
    kLoop,
    kLoope,
    kLoopne,
    kJrcxz,
    kSetcc,
    // Sign extension.
    kCwde,
    kCdqe,
    kCdq,
    kCqo,
    // No-ops and fences.
    kNop,
    kEndbr64,
    kPause,
    kLfence,
    kMfence,
    kSfence,
    // String instructions.
    kMovsb,
    kMovsw,
    kMovsd,
    kMovsq,
    kStosb,
    kStosw,
    kStosd,
    kStosq,
    kLodsb,
    kLodsw,
    kLodsd,
    kLodsq,
    kScasb,
    kScasw,
    kScasd,
    kScasq,
    kCmpsb,
    kCmpsw,
    kCmpsd,
    kCmpsq,
    // System.
    kCpuid,
    kRdtsc,
    kRdmsr,
    kWrmsr,
    kRdrand,
    kPsrldq,
    kPslldq,
    kShufps,
    kMovlps,
    kMovhps,
    kMovlpd,
    kMovlhps,
    kMovhlps,
    kPmovmskb,
    kSyscall,
    kSysret,
    kIretq,
    kLretq,
    kSldt,
    kStr,
    kLldt,
    kLtr,
    kSwapgs,
    kInvlpg,
    kLgdt,
    kLidt,
    kSgdt,
    kSidt,
    kHlt,
    kPushfq,
    kPopfq,
    kCli,
    kSti,
    kIn,
    kOut,
    kCld,
    kStd,
    kBswap,
    kTzcnt,
    kLzcnt,
    kFxsave,
    kFxrstor,
    kFxsave64,
    kFxrstor64,
    // SSE2 subset (spec section 5.2 list).
    kMovaps,
    kMovups,
    kMovdqa,
    kMovdqu,
    kMovq_xmm,
    kMovd_xmm,
    kPshufd,
    kPshuflw,
    kPshufhw,
    kPunpcklbw,
    kPunpcklwd,
    kPunpckldq,
    kPunpcklqdq,
    kPunpckhbw,
    kPunpckhwd,
    kPunpckhdq,
    kPunpckhqdq,
    kPackuswb,
    kPxor,
    kPand,
    kPandn,
    kPor,
    kPsrlw,
    kPsrld,
    kPsrlq,
    kPsllw,
    kPslld,
    kPsllq,
    kPaddd,
    kPaddq,
    kPsubd,
    kPaddb,
    kPsubb,
    kPaddw,
    kPsubw,
    kPsubq,
    kPminub,
    kPmaxub,
    kPminsw,
    kPmaxsw,
    kRdsspq,
    kPcmpeqb,
    kPcmpeqd,
    kPcmpeqq,
    // New-kernel instruction face (2026-11-10 profile over the r35 ACPI/
    // SMP/uaccess paths; see document/notes for the audit).
    kPsadbw,
    kPextrw,
    kMovss,
    kClac,
    kStac,
    kFninit,
    kUd2,
};

/**
 * @brief One resolved operand of a decoded instruction.
 *
 * Intel operand order: ops[0] is the destination (first operand in
 * Intel syntax, last in AT&T).
 */
struct Operand {
    /// @brief Operand storage class after ModRM/SIB/immediate resolution.
    enum class Kind : uint8_t {
        kNone,     ///< No operand in this slot.
        kReg,      ///< Register (GPR, XMM, CR, DR, or segment).
        kMem,      ///< Memory effective address [base + index*scale + disp].
        kImm,      ///< Immediate.
        kImplicit, ///< Implicit fixed register (al, cl, dx, ...), set by
                   ///< table semantics, not by encoding.
    };
    Kind kind = Kind::kNone;
    /// @brief Operand byte width: 1, 2, 4, 8 (GPR) or 16 (XMM memory).
    uint8_t size = 0;
    /// @brief Register id for kReg / kImplicit.
    Reg reg = Reg::kRax;
    // --- memory fields (kMem) ---
    bool has_base = false;     ///< Base register present.
    Reg base = Reg::kRax;      ///< Base register (kMem).
    bool has_index = false;    ///< Index register present.
    Reg index = Reg::kRax;     ///< Index register (kMem).
    uint8_t scale = 1;         ///< Index scale factor: 1, 2, 4, or 8.
    int64_t disp = 0;          ///< Displacement (kMem) or value (kImm).
    bool rip_relative = false; ///< [rip + disp] (64-bit addressing only).
    bool has_disp = false;     ///< Displacement bytes were present (mod 1/2).
};

/// @brief Segment override prefix (fs/gs carry per-cpu base semantics in
///        the executor).
enum class Seg : uint8_t { kNone, kEs, kCs, kSs, kDs, kFs, kGs };

/**
 * @brief One decoded instruction: length, prefixes, mnemonic, operands.
 *
 * The decoder resolves everything byte-level (prefixes, REX, ModRM, SIB,
 * displacement, immediate) into this flat record. The executor (batch 3)
 * consumes it without re-reading instruction bytes.
 */
struct DecodedInsn {
    Mnemonic mnemonic = Mnemonic::kNop;
    /// @brief Total instruction length in bytes, prefixes included.
    uint8_t length = 0;
    /// @brief Condition code for jcc/setcc(cmovcc)/loop guard families.
    Cc cc = Cc::kO;
    /// @brief Effective operand size in bytes (1/2/4/8/16) after 66/REX.W.
    uint8_t opsize = 4;
    /// @brief Effective address size in bytes (4/8) after 67.
    uint8_t addr_size = 8;
    /// @brief Lock prefix present.
    bool has_lock = false;
    /// @brief Rep-family prefix state.
    Rep rep = Rep::kNone;
    /// @brief Segment override prefix.
    Seg seg = Seg::kNone;
    /// @brief A REX prefix was present (disambiguates spl/bpl/sil/dil from
    ///        ah/ch/dh/bh at width 1 when rendering register names).
    bool rex_present = false;
    /// @brief Operands in Intel order; unused slots keep Kind::kNone.
    Operand ops[3];
};

} // namespace micro_forge::arch::x86_64
