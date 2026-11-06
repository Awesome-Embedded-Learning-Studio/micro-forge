/**
 * @file internal.hpp
 * @brief Internal decoder plumbing shared across the x86-64 translation
 *        units: slot types, tables, and register/width helpers. Not part
 *        of the public API.
 * @version 0.1.0
 */

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "micro_forge/arch/x86_64/decode_error.hpp"
#include "micro_forge/arch/x86_64/decoded.hpp"

namespace micro_forge::arch::x86_64::detail {

/// @brief Operand slot template: how one operand is sourced.
enum class Slot : uint8_t {
    kNone,  ///< Unused slot.
    kRm,    ///< ModRM r/m, GPR or memory, width = opsize.
    kRm8,   ///< ModRM r/m, width fixed 1.
    kRm16,  ///< ModRM r/m, width fixed 2.
    kRm32,  ///< ModRM r/m, width fixed 4 (movsxd source).
    kReg,   ///< ModRM reg field, GPR, width = opsize.
    kReg8,  ///< ModRM reg field, GPR, width fixed 1.
    kOpr,   ///< GPR encoded in the opcode low 3 bits (+REX.B), width=opsize.
    kOpr8,  ///< GPR in opcode low 3 bits, width fixed 1.
    kXReg,  ///< ModRM reg field, XMM.
    kXRm,   ///< ModRM r/m, XMM or 16-byte memory.
    kXRm64, ///< ModRM r/m, XMM or 8-byte memory (movq).
    kXRm32, ///< ModRM r/m, XMM or 4-byte memory (movd).
    kRm32S, ///< ModRM r/m as GPR or 4-byte memory for SSE-styled entries.
    kRm64f, ///< ModRM r/m as GPR or 8-byte memory (movq r64 <-> xmm).
    kAcc,   ///< Implicit accumulator, width = opsize.
    kAcc8,  ///< Implicit al.
    kCl,    ///< Implicit cl.
    kDx,    ///< Implicit dx.
    kOne,   ///< Implicit immediate 1.
    kMem,   ///< Memory operand (lea via ModRM, moffs via immediate).
    kSreg,  ///< Segment register from ModRM reg field.
    kCr,    ///< CR from ModRM reg field.
    kDr,    ///< DR from ModRM reg field.
};

/// @brief How the effective operand size is derived.
enum class SizeR : uint8_t {
    kD, ///< Default: REX.W -> 8, 66 -> 2, else 4.
    kB, ///< Fixed 1.
    kQ, ///< Fixed 8.
    kV, ///< Fixed 16 (SSE).
    kP, ///< push/pop: 66 -> 2, else 8.
};

/// @brief Trailing-immediate width rule.
enum class ImmK : uint8_t {
    k0,   ///< No trailing immediate.
    k8,   ///< Raw imm8.
    k8S,  ///< imm8, sign-extended at decode time.
    k16,  ///< imm16.
    k32S, ///< imm32, sign-extended at decode time.
    kSz4, ///< imm at min(opsize, 4); mov r,imm reads imm64 under REX.W.
    k64,  ///< imm64 (also moffs64).
};

constexpr uint8_t kNoImm = 0xFF;

/// @brief One opcode table entry.
struct Ent {
    Mnemonic mn = Mnemonic::kNop;
    Slot a = Slot::kNone;
    Slot b = Slot::kNone;
    Slot c = Slot::kNone;
    uint8_t imm_slot = kNoImm;
    SizeR sz = SizeR::kD;
    ImmK imm = ImmK::k0;
    uint8_t grp = 0;
    bool valid = false;
};

/// @brief Builds a table entry with defaults (slots, sizes, groups).
[[nodiscard]] constexpr Ent make_ent(Mnemonic mn, Slot a = Slot::kNone, Slot b = Slot::kNone,
                                     Slot c = Slot::kNone, SizeR sz = SizeR::kD,
                                     ImmK imm = ImmK::k0, uint8_t islot = kNoImm, uint8_t grp = 0) {
    return Ent{.mn = mn,
               .a = a,
               .b = b,
               .c = c,
               .imm_slot = islot,
               .sz = sz,
               .imm = imm,
               .grp = grp,
               .valid = true};
}

/// @brief Group-opcode ids dispatched on the ModRM reg field.
enum Groups : uint8_t {
    kGrpNone = 0,
    kGrp1,  // 80/81/83: ALU + imm
    kGrp2,  // C0/C1/D0-D3: shifts
    kGrp3,  // F6/F7: test/not/neg/mul/imul/div/idiv
    kGrp4,  // FE: inc/dec rm8
    kGrp5,  // FF: inc/dec/call/jmp/push r/m
    kGrp6,  // 0F 01: descriptor tables / invlpg / swapgs
    kGrp8,  // 0F BA: bt/bts/btr/btc + imm8
    kGrp9,  // 0F C7: rdrand
    kGrpPs, // 0F 71-73 (66): packed shifts + imm8
    kGrpAe, // 0F AE: fxsave/fxrstor/fences
};

/// @brief Byte-stream cursor with bounds-checked consumption.
struct Stream {
    std::span<const uint8_t> bytes;
    uint64_t pos = 0;

    [[nodiscard]] bool has(uint64_t n) const { return pos + n <= bytes.size(); }
    [[nodiscard]] uint8_t peek() const { return bytes[static_cast<size_t>(pos)]; }
    /// @brief Consumes and returns the byte at the cursor.
    ///
    /// @return The byte at the cursor; the cursor advances by one.
    uint8_t next() { return bytes[static_cast<size_t>(pos++)]; }
    /// @brief Un-consumes one byte (REX re-inspection).
    void back() { --pos; }
};

/// @brief Decoded prefix state: legacy prefixes plus REX bits.
struct Pfx {
    bool lock = false;
    Rep rep = Rep::kNone;
    Seg seg = Seg::kNone;
    bool opsz66 = false;
    bool addr67 = false;
    bool rex = false;
    uint8_t rex_w = 0, rex_r = 0, rex_x = 0, rex_b = 0;
};

[[nodiscard]] inline uint8_t slot_width(Slot s, uint8_t opsize) {
    switch (s) {
    case Slot::kRm:
    case Slot::kReg:
    case Slot::kOpr:
    case Slot::kMem:
    case Slot::kAcc:
        return opsize;
    case Slot::kRm8:
    case Slot::kReg8:
    case Slot::kOpr8:
    case Slot::kAcc8:
    case Slot::kCl:
    case Slot::kOne:
        return 1;
    case Slot::kRm16:
    case Slot::kDx:
    case Slot::kSreg:
        return 2;
    case Slot::kRm32:
        return 4;
    case Slot::kXReg:
    case Slot::kXRm:
        return 16;
    case Slot::kXRm64:
    case Slot::kCr:
    case Slot::kDr:
        return 8;
    case Slot::kXRm32:
    case Slot::kRm32S:
        return 4;
    case Slot::kRm64f:
        return 8;
    default:
        return 0;
    }
}

[[nodiscard]] inline bool slot_is_rm(Slot s) {
    switch (s) {
    case Slot::kRm:
    case Slot::kRm8:
    case Slot::kRm16:
    case Slot::kRm32:
    case Slot::kRm32S:
    case Slot::kRm64f:
    case Slot::kXRm:
    case Slot::kXRm32:
    case Slot::kXRm64:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool slot_is_regfield(Slot s) {
    switch (s) {
    case Slot::kReg:
    case Slot::kReg8:
    case Slot::kXReg:
    case Slot::kSreg:
    case Slot::kCr:
    case Slot::kDr:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline Reg gpr_at(uint8_t code, uint8_t ext) {
    return static_cast<Reg>((static_cast<unsigned>(code) & 7U) | (ext != 0U ? 8U : 0U));
}

[[nodiscard]] inline Reg xmm_at(uint8_t code, uint8_t ext) {
    return static_cast<Reg>(16 + ((static_cast<unsigned>(code) & 7U) | (ext != 0U ? 8U : 0U)));
}

[[nodiscard]] inline Reg sreg_at(uint8_t code) {
    switch (static_cast<unsigned>(code) & 7U) {
    case 0:
        return Reg::kEs;
    case 1:
        return Reg::kCs;
    case 2:
        return Reg::kSs;
    case 3:
        return Reg::kDs;
    case 4:
        return Reg::kFs;
    default:
        return Reg::kGs;
    }
}

[[nodiscard]] inline Reg cr_at(uint8_t code) {
    switch (static_cast<unsigned>(code) & 7U) {
    case 0:
        return Reg::kCr0;
    case 2:
        return Reg::kCr2;
    case 3:
        return Reg::kCr3;
    default:
        return Reg::kCr4; // reg 4
    }
}

[[nodiscard]] inline Reg dr_at(uint8_t code) {
    switch (static_cast<unsigned>(code) & 7U) {
    case 0:
        return Reg::kDr0;
    case 1:
        return Reg::kDr1;
    case 2:
        return Reg::kDr2;
    case 3:
        return Reg::kDr3;
    case 6:
        return Reg::kDr6;
    default:
        return Reg::kDr7; // reg 7
    }
}

/// @brief Strict endbr64 tail check: F3 0F 1E FA is /7, mod=3, rm=2.
[[nodiscard]] inline bool is_endbr64_tail(uint8_t modrm) {
    return (modrm >> 6) == 3 && ((modrm >> 3) & 7U) == 7 && (modrm & 7U) == 2;
}

/// @brief CR/DR whitelist: CR0/2/3/4 and DR0-3/6/7 only. Reserved
///        encodings must be rejected, not aliased to a neighbor.
/// @return true when @p code (reg field, REX.R-extended) names a register
///         in the whitelist for the given bank.
[[nodiscard]] inline bool cr_dr_ok(unsigned code, bool is_cr) {
    if (is_cr) {
        return code == 0 || code == 2 || code == 3 || code == 4;
    }
    return code <= 3 || code == 6 || code == 7;
}

/// @brief Whether an entry needs a ModRM byte (any r/m or reg-field slot,
///        or group dispatch).
[[nodiscard]] inline bool wants_modrm(const Ent& ent) {
    return ent.grp != 0 || slot_is_rm(ent.a) || slot_is_rm(ent.b) || slot_is_rm(ent.c) ||
           slot_is_regfield(ent.a) || slot_is_regfield(ent.b) || slot_is_regfield(ent.c);
}

/// @brief Picks the operand-size variant of a string mnemonic family.
[[nodiscard]] inline Mnemonic string_mnemonic(Mnemonic b8, Mnemonic b16, Mnemonic b32, Mnemonic b64,
                                              uint8_t opsize) {
    if (opsize == 1) {
        return b8;
    }
    if (opsize == 2) {
        return b16;
    }
    if (opsize == 8) {
        return b64;
    }
    return b32;
}

/// @brief Final mnemonic fixups after the table lookup: cc families,
///        64-bit movq variants, movabs, string families, wideners.
inline void finalize_mnemonic(DecodedInsn& out, Ent& ent, const Pfx& p, bool esc, uint8_t op,
                              uint8_t esc_op, uint8_t opsize) {
    // cc-bearing families take the condition from the opcode low nibble.
    if (esc) {
        if (ent.mn == Mnemonic::kJcc || ent.mn == Mnemonic::kSetcc || ent.mn == Mnemonic::kCmovcc) {
            out.cc = static_cast<Cc>(static_cast<unsigned>(esc_op) & 0xFU);
        }
    } else if (ent.mn == Mnemonic::kJcc) {
        out.cc = static_cast<Cc>(static_cast<unsigned>(op) & 0xFU);
    }

    // 66 REX.W 0F 6E/7E are MOVQ (64-bit) rather than MOVD (32-bit).
    if (esc && (p.rex_w != 0U) && ent.mn == Mnemonic::kMovd_xmm) {
        ent.mn = Mnemonic::kMovq_xmm;
        ent.sz = SizeR::kQ;
        out.mnemonic = ent.mn;
        out.opsize = 8;
        if (ent.a == Slot::kXReg) {
            ent.b = Slot::kRm64f; // 6E: movq xmm, r/m64
        } else {
            ent.a = Slot::kRm64f; // 7E: movq r/m64, xmm
        }
    }

    // mov r64, imm64 (B8+ under REX.W) is rendered movabs by objdump.
    if (!esc && op >= 0xB8 && op <= 0xBF && (p.rex_w != 0U)) {
        out.mnemonic = Mnemonic::kMovabs;
    }

    // String / widening families pick their mnemonic from operand size.
    if (!esc) {
        switch (op) {
        case 0xA5:
            out.mnemonic = string_mnemonic(Mnemonic::kMovsb, Mnemonic::kMovsw, Mnemonic::kMovsd,
                                           Mnemonic::kMovsq, opsize);
            break;
        case 0xA7:
            out.mnemonic = string_mnemonic(Mnemonic::kCmpsb, Mnemonic::kCmpsw, Mnemonic::kCmpsd,
                                           Mnemonic::kCmpsq, opsize);
            break;
        case 0xAB:
            out.mnemonic = string_mnemonic(Mnemonic::kStosb, Mnemonic::kStosw, Mnemonic::kStosd,
                                           Mnemonic::kStosq, opsize);
            break;
        case 0xAD:
            out.mnemonic = string_mnemonic(Mnemonic::kLodsb, Mnemonic::kLodsw, Mnemonic::kLodsd,
                                           Mnemonic::kLodsq, opsize);
            break;
        case 0xAF:
            out.mnemonic = string_mnemonic(Mnemonic::kScasb, Mnemonic::kScasw, Mnemonic::kScasd,
                                           Mnemonic::kScasq, opsize);
            break;
        case 0x98:
            out.mnemonic = (p.rex_w != 0U) ? Mnemonic::kCdqe : Mnemonic::kCwde;
            break;
        case 0x99:
            out.mnemonic = (p.rex_w != 0U) ? Mnemonic::kCqo : Mnemonic::kCdq;
            break;
        default:
            break;
        }
    }
}

// --- opcode tables (defined in tables.cpp) ---
/// @brief The four 0F escape tables indexed by mandatory prefix.
struct EscTables {
    std::array<Ent, 256> none;
    std::array<Ent, 256> p66;
    std::array<Ent, 256> p2;
    std::array<Ent, 256> p3;
};

extern const std::array<Ent, 256> kPrimary;
extern const EscTables kEsc;
extern const std::array<Ent, 256> kEsc38;

// --- group mnemonic resolution (tables.cpp) ---
[[nodiscard]] Mnemonic grp1_mn(uint8_t reg);
[[nodiscard]] Mnemonic grp2_mn(uint8_t reg);
[[nodiscard]] Mnemonic grp3_mn(uint8_t reg);
[[nodiscard]] Mnemonic grp4_mn(uint8_t reg);
[[nodiscard]] Mnemonic grp5_mn(uint8_t reg);

/// @brief Parses legacy prefixes and REX from @p st into @p p.
///
/// @return false when a 67 (16-bit addressing) prefix is seen: it is
///         outside the v0 whitelist and the stream position is left on
///         the offending byte for the error report.
[[nodiscard]] inline bool parse_prefixes(Stream& st, Pfx& p) {
    for (;;) {
        if (!st.has(1)) {
            return false;
        }
        const uint8_t b = st.peek();
        if (b == 0x67) {
            // 16-bit addressing is outside the v0 whitelist; leave pos on
            // the offending byte.
            return false;
        }
        if (b == 0x66) {
            p.opsz66 = true;
        } else if (b == 0xF0) {
            p.lock = true;
        } else if (b == 0xF2) {
            p.rep = Rep::kRepne;
        } else if (b == 0xF3) {
            p.rep = Rep::kRep;
        } else if (b == 0x26 || b == 0x2E || b == 0x36 || b == 0x3E || b == 0x64 || b == 0x65) {
            switch (b) {
            case 0x26:
                p.seg = Seg::kEs;
                break;
            case 0x2E:
                p.seg = Seg::kCs;
                break;
            case 0x36:
                p.seg = Seg::kSs;
                break;
            case 0x3E:
                p.seg = Seg::kDs;
                break;
            case 0x64:
                p.seg = Seg::kFs;
                break;
            default:
                p.seg = Seg::kGs;
                break;
            }
        } else if (b >= 0x40 && b <= 0x4F) {
            p.rex = true;
            p.rex_w = static_cast<uint8_t>((static_cast<unsigned>(b) >> 3U) & 1U);
            p.rex_r = static_cast<uint8_t>((static_cast<unsigned>(b) >> 2U) & 1U);
            p.rex_x = static_cast<uint8_t>((static_cast<unsigned>(b) >> 1U) & 1U);
            p.rex_b = static_cast<uint8_t>(static_cast<unsigned>(b) & 1U);
        } else {
            return true;
        }
        st.next();
    }
}

} // namespace micro_forge::arch::x86_64::detail
