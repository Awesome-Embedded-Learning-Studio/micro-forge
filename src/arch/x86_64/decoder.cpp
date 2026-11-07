/**
 * @file decoder.cpp
 * @brief decode_one: prefix -> opcode -> ModRM/SIB -> immediate pipeline.
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/decoder.hpp"

#include "micro_forge/kernel/bit.hpp"
#include "micro_forge/kernel/le_access.hpp"

#include "arch/x86_64/internal.hpp"

namespace micro_forge::arch::x86_64 {

using namespace detail;

std::expected<DecodedInsn, DecodeError> decode_one(std::span<const uint8_t> bytes) {
    Stream st{.bytes = bytes};
    Pfx p;

    // --- prefix pass (67 rejected: outside the v0 whitelist) ---
    if (!detail::parse_prefixes(st, p)) {
        const auto kind = st.has(1) ? DecodeError::Kind::kBadPrefix : DecodeError::Kind::kTruncated;
        return std::unexpected(DecodeError{.kind = kind, .offset = st.pos});
    }
    if (!st.has(1)) {
        return std::unexpected(
            DecodeError{.kind = DecodeError::Kind::kTruncated, .offset = st.pos});
    }

    // --- opcode selection ---
    const uint8_t op = st.next();
    const uint64_t opcode_off = st.pos - 1;
    Ent ent{};
    bool esc = false;
    uint8_t esc_op = 0;
    if (op == 0x0F) {
        if (!st.has(1)) {
            return std::unexpected(
                DecodeError{.kind = DecodeError::Kind::kTruncated, .offset = st.pos});
        }
        esc = true;
        esc_op = st.next();
        if (esc_op == 0x38 || esc_op == 0x3A) {
            // Three-byte opcode: one more byte, only 0F38+66 has entries.
            if (!st.has(1)) {
                return std::unexpected(
                    DecodeError{.kind = DecodeError::Kind::kTruncated, .offset = st.pos});
            }
            const uint8_t op3 = st.next();
            if (esc_op == 0x38 && p.opsz66 && p.rep == Rep::kNone) {
                ent = kEsc38[op3];
                p.opsz66 = false; // consumed as mandatory prefix
            }
            if (!ent.valid) {
                return std::unexpected(DecodeError{
                    .kind = DecodeError::Kind::kUnsupportedOpcode,
                    .offset = opcode_off,
                });
            }
        } else {
            unsigned pmod = 0U;
            if (p.rep == Rep::kRep) {
                pmod = 3U;
            } else if (p.rep == Rep::kRepne) {
                pmod = 2U;
            } else if (p.opsz66) {
                pmod = 1U;
            }
            const Ent* direct = nullptr;
            switch (pmod) {
            case 1:
                direct = &kEsc.p66[esc_op];
                break;
            case 3:
                direct = &kEsc.p3[esc_op];
                break;
            default:
                break;
            }
            const Ent& plain = kEsc.none[esc_op];
            if (direct != nullptr && direct->valid) {
                ent = *direct;
                // Mandatory prefix consumed by the opcode loses its
                // prefix meaning.
                if (pmod == 3) {
                    p.rep = Rep::kNone;
                }
                if (pmod == 1) {
                    p.opsz66 = false;
                }
            } else if (plain.valid) {
                ent = plain;
                // A rep prefix that was not consumed as a mandatory prefix
                // has no semantics on 0F opcodes: drop it from the IR.
                p.rep = Rep::kNone;
            } else {
                return std::unexpected(DecodeError{
                    .kind = DecodeError::Kind::kUnsupportedOpcode,
                    .offset = opcode_off,
                });
            }
        }
    } else {
        ent = kPrimary[op];
        if (!ent.valid) {
            return std::unexpected(
                DecodeError{.kind = DecodeError::Kind::kUnsupportedOpcode, .offset = opcode_off});
        }
        if (op == 0x90 && p.rep == Rep::kRep) {
            // F3 90 = pause.
            ent = make_ent(Mnemonic::kPause);
            p.rep = Rep::kNone;
        } else if (p.rep != Rep::kNone && (op < 0xA4 || op > 0xAF)) {
            // rep only means something on the string families.
            p.rep = Rep::kNone;
        }
    }

    // --- effective operand size ---
    uint8_t opsize = 4;
    switch (ent.sz) {
    case SizeR::kD:
        if (p.rex_w != 0U) {
            opsize = 8;
        } else {
            opsize = p.opsz66 ? 2 : 4;
        }
        break;
    case SizeR::kB:
        opsize = 1;
        break;
    case SizeR::kQ:
        opsize = 8;
        break;
    case SizeR::kV:
        opsize = 16;
        break;
    case SizeR::kP:
        if (p.rex_w != 0U) {
            opsize = 8;
        } else {
            opsize = p.opsz66 ? 2 : 8;
        }
        break;
    default:
        break;
    }

    DecodedInsn out;
    out.mnemonic = ent.mn;
    out.opsize = opsize;
    out.addr_size = 8;
    out.has_lock = p.lock;
    out.rep = p.rep;
    out.seg = p.seg;
    out.rex_present = p.rex;

    detail::finalize_mnemonic(out, ent, p, esc, op, esc_op, opsize);

    // --- ModRM ---
    const bool wants_modrm =
        ent.grp != 0 || slot_is_rm(ent.a) || slot_is_rm(ent.b) || slot_is_rm(ent.c) ||
        slot_is_regfield(ent.a) || slot_is_regfield(ent.b) || slot_is_regfield(ent.c) ||
        (ent.a == Slot::kMem && op != 0xA0 && op != 0xA1 && op != 0xA2 && op != 0xA3 && !esc);
    uint8_t modrm = 0;
    bool have_modrm = false;
    if (wants_modrm) {
        if (!st.has(1)) {
            return std::unexpected(
                DecodeError{.kind = DecodeError::Kind::kTruncated, .offset = st.pos});
        }
        modrm = st.next();
        have_modrm = true;
        const auto mod = static_cast<uint8_t>(kernel::bit::extract(modrm, kModrmMod));
        const auto reg = static_cast<uint8_t>(kernel::bit::extract(modrm, kModrmReg));
        const auto rm = static_cast<uint8_t>(kernel::bit::extract(modrm, kModrmRm));
        const bool modreg = (mod == 3);
        const uint64_t bad = opcode_off;

        switch (ent.grp) {
        case kGrp1:
            out.mnemonic = grp1_mn(reg);
            break;
        case kGrp2:
            out.mnemonic = grp2_mn(reg);
            if (out.mnemonic == Mnemonic::kNop) {
                return std::unexpected(
                    DecodeError{.kind = DecodeError::Kind::kUnsupportedOpcode, .offset = bad});
            }
            break;
        case kGrp3:
            if (reg <= 1) {
                out.mnemonic = Mnemonic::kTest;
                ent.imm = opsize == 1 ? ImmK::k8 : ImmK::kSz4;
                ent.imm_slot = 1;
            } else {
                out.mnemonic = grp3_mn(reg);
                if (out.mnemonic == Mnemonic::kNop) {
                    return std::unexpected(
                        DecodeError{.kind = DecodeError::Kind::kUnsupportedOpcode, .offset = bad});
                }
            }
            break;
        case kGrp4:
            out.mnemonic = grp4_mn(reg);
            if (out.mnemonic == Mnemonic::kNop) {
                return std::unexpected(
                    DecodeError{.kind = DecodeError::Kind::kUnsupportedOpcode, .offset = bad});
            }
            break;
        case kGrp5:
            out.mnemonic = grp5_mn(reg);
            if (out.mnemonic == Mnemonic::kNop) {
                return std::unexpected(
                    DecodeError{.kind = DecodeError::Kind::kUnsupportedOpcode, .offset = bad});
            }
            // inc/dec default to 32-bit operands; call/jmp/push stay 64.
            ent.sz = (reg <= 1) ? SizeR::kD : SizeR::kQ;
            if (reg > 1) {
                opsize = 8;
            }
            out.opsize = opsize;
            break;
        case kGrp6:
            if (modreg && reg == 7 && rm == 0) {
                out.mnemonic = Mnemonic::kSwapgs;
                ent.a = Slot::kNone;
            } else if (!modreg && reg == 7) {
                out.mnemonic = Mnemonic::kInvlpg;
                ent.a = Slot::kRm8; // objdump renders BYTE PTR
            } else {
                switch (reg) {
                case 0:
                    out.mnemonic = Mnemonic::kSgdt;
                    break;
                case 1:
                    out.mnemonic = Mnemonic::kSidt;
                    break;
                case 2:
                    out.mnemonic = Mnemonic::kLgdt;
                    break;
                case 3:
                    out.mnemonic = Mnemonic::kLidt;
                    break;
                default:
                    return std::unexpected(
                        DecodeError{.kind = DecodeError::Kind::kUnsupportedOpcode, .offset = bad});
                }
            }
            break;
        case kGrp8:
            if (reg < 3) {
                return std::unexpected(
                    DecodeError{.kind = DecodeError::Kind::kUnsupportedOpcode, .offset = bad});
            }
            if (reg == 3) {
                out.mnemonic = Mnemonic::kBt;
            } else if (reg == 4) {
                out.mnemonic = Mnemonic::kBts;
            } else if (reg == 5) {
                out.mnemonic = Mnemonic::kBtr;
            } else {
                out.mnemonic = Mnemonic::kBtc;
            }
            break;
        case kGrp9:
            if (reg != 6 || !modreg) {
                return std::unexpected(
                    DecodeError{.kind = DecodeError::Kind::kUnsupportedOpcode, .offset = bad});
            }
            out.mnemonic = Mnemonic::kRdrand;
            ent.a = Slot::kRm; // register form via modrm r/m
            break;
        case kGrpPs: {
            if (kernel::bit::extract(modrm, kModrmMod) != 3) {
                // Packed-shift immediate forms are register-only.
                return std::unexpected(
                    DecodeError{.kind = DecodeError::Kind::kUnsupportedOpcode, .offset = bad});
            }
            const auto base = static_cast<uint8_t>(esc_op - 0x71);
            const bool is_ll = reg == 6;
            const bool is_rl = reg == 2;
            if (!is_ll && !is_rl) {
                return std::unexpected(
                    DecodeError{.kind = DecodeError::Kind::kUnsupportedOpcode, .offset = bad});
            }
            const bool is_w = base == 0;
            const bool is_d = base == 1;
            if (is_w) {
                out.mnemonic = is_ll ? Mnemonic::kPsllw : Mnemonic::kPsrlw;
            } else if (is_d) {
                out.mnemonic = is_ll ? Mnemonic::kPslld : Mnemonic::kPsrld;
            } else {
                out.mnemonic = is_ll ? Mnemonic::kPsllq : Mnemonic::kPsrlq;
            }
            // The group selector is the reg field; the destination XMM is
            // the r/m field (verified against objdump).
            ent.a = Slot::kXRm;
            break;
        }
        case kGrpAe:
            if (!modreg && reg == 0) {
                out.mnemonic = (p.rex_w != 0U) ? Mnemonic::kFxsave64 : Mnemonic::kFxsave;
            } else if (!modreg && reg == 1) {
                out.mnemonic = (p.rex_w != 0U) ? Mnemonic::kFxrstor64 : Mnemonic::kFxrstor;
            } else if (modreg && reg == 5) {
                out.mnemonic = Mnemonic::kLfence;
            } else if (modreg && reg == 6) {
                out.mnemonic = Mnemonic::kMfence;
            } else if (modreg && reg == 7) {
                out.mnemonic = Mnemonic::kSfence;
            } else {
                return std::unexpected(
                    DecodeError{.kind = DecodeError::Kind::kUnsupportedOpcode, .offset = bad});
            }
            if (modreg) {
                ent.a = Slot::kNone;
            }
            break;
        default:
            break;
        }
    }

    // endbr64: F3 0F 1E FA exactly.
    if (out.mnemonic == Mnemonic::kEndbr64) {
        if (!have_modrm || !is_endbr64_tail(modrm)) {
            return std::unexpected(
                DecodeError{.kind = DecodeError::Kind::kUnsupportedOpcode, .offset = opcode_off});
        }
        ent.a = Slot::kNone; // operand was only there to consume ModRM
    }

    // CR/DR whitelist (CR0/2/3/4, DR0-3/6/7): the number lives in the
    // reg field and requires the register form (mod == 3) for r/m.
    if (have_modrm &&
        (ent.a == Slot::kCr || ent.b == Slot::kCr || ent.a == Slot::kDr || ent.b == Slot::kDr)) {
        const auto code = static_cast<uint8_t>(kernel::bit::extract(modrm, kModrmReg) |
                                               ((p.rex_r != 0U) ? 8U : 0U));
        const bool is_cr = ent.a == Slot::kCr || ent.b == Slot::kCr;
        if (!cr_dr_ok(code, is_cr) || kernel::bit::extract(modrm, kModrmMod) != 3) {
            return std::unexpected(
                DecodeError{.kind = DecodeError::Kind::kUnsupportedOpcode, .offset = opcode_off});
        }
    }

    // lea rejects register-form ModRM.
    if (out.mnemonic == Mnemonic::kLea && have_modrm &&
        kernel::bit::extract(modrm, kModrmMod) == 3) {
        return std::unexpected(
            DecodeError{.kind = DecodeError::Kind::kBadModrm, .offset = st.pos - 1});
    }

    // --- operand assembly ---
    const uint8_t regf = have_modrm ? static_cast<uint8_t>(kernel::bit::extract(modrm, kModrmReg))
                                    : static_cast<uint8_t>(static_cast<unsigned>(op) & 7U);

    const Slot slots[3] = {ent.a, ent.b, ent.c};
    for (unsigned i = 0; i < 3; ++i) {
        Operand& dst = out.ops[i];
        const Slot s = slots[i];
        if (s == Slot::kNone) {
            continue;
        }
        dst.size = slot_width(s, opsize);
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
            if (!resolve_rm(st, modrm, opsize, p, s, dst)) {
                return std::unexpected(
                    DecodeError{.kind = DecodeError::Kind::kTruncated, .offset = st.pos});
            }
            break;
        case Slot::kCr:
            dst.kind = Operand::Kind::kReg;
            dst.reg = cr_at(regf);
            break;
        case Slot::kDr:
            dst.kind = Operand::Kind::kReg;
            dst.reg = dr_at(regf);
            break;
        case Slot::kMem:
            if (have_modrm) {
                if (!resolve_rm(st, modrm, opsize, p, Slot::kRm, dst)) {
                    return std::unexpected(
                        DecodeError{.kind = DecodeError::Kind::kTruncated, .offset = st.pos});
                }
                if (dst.kind == Operand::Kind::kReg) {
                    return std::unexpected(
                        DecodeError{.kind = DecodeError::Kind::kBadModrm, .offset = st.pos - 1});
                }
            } else {
                // A0-A3 moffs: kMem whose address arrives as the trailing
                // immediate (a plain immediate would skip the bus).
                dst.kind = Operand::Kind::kMem;
            }
            break;
        case Slot::kReg:
        case Slot::kReg8:
            dst.kind = Operand::Kind::kReg;
            dst.reg = gpr_at(regf, p.rex_r);
            break;
        case Slot::kOpr:
        case Slot::kOpr8:
            dst.kind = Operand::Kind::kReg;
            dst.reg = gpr_at(static_cast<uint8_t>(static_cast<unsigned>(op) & 7U), p.rex_b);
            break;
        case Slot::kXReg:
            dst.kind = Operand::Kind::kReg;
            dst.reg = xmm_at(regf, p.rex_r);
            break;
        case Slot::kSreg:
            if (regf > 5) {
                return std::unexpected(DecodeError{
                    .kind = DecodeError::Kind::kUnsupportedOpcode,
                    .offset = opcode_off,
                });
            }
            dst.kind = Operand::Kind::kReg;
            dst.reg = sreg_at(regf);
            break;
        case Slot::kAcc:
        case Slot::kAcc8:
            dst.kind = Operand::Kind::kImplicit;
            dst.reg = Reg::kRax;
            break;
        case Slot::kCl:
            dst.kind = Operand::Kind::kImplicit;
            dst.reg = Reg::kRcx;
            break;
        case Slot::kDx:
            dst.kind = Operand::Kind::kImplicit;
            dst.reg = Reg::kRdx;
            break;
        case Slot::kOne:
            dst.kind = Operand::Kind::kImm;
            dst.disp = 1;
            dst.size = 0; // implicit: rendered decimal ("shr rax,1")
            break;
        default:
            break;
        }
    }

    // mov sreg: memory renders WORD PTR, GPRs render 32-bit names.
    if (out.mnemonic == Mnemonic::kMov && (ent.a == Slot::kSreg || ent.b == Slot::kSreg)) {
        for (Operand& o : out.ops) {
            if (o.kind == Operand::Kind::kReg && static_cast<unsigned>(o.reg) < 16) {
                o.size = 4;
            } else if (o.kind == Operand::Kind::kMem) {
                o.size = 2;
            }
        }
    }

    // --- trailing immediate / moffs ---
    if (ent.imm != ImmK::k0 && ent.imm_slot != kNoImm) {
        uint8_t isz = 0;
        bool sext = false;
        switch (ent.imm) {
        case ImmK::k8:
            isz = 1;
            break;
        case ImmK::k8S:
            isz = 1;
            sext = true;
            break;
        case ImmK::k16:
            isz = 2;
            break;
        case ImmK::k32S:
            isz = 4;
            sext = true;
            break;
        case ImmK::k64:
            isz = 8;
            break;
        case ImmK::kSz4:
            if (!esc && op >= 0xB8 && op <= 0xBF) {
                if (p.rex_w != 0U) {
                    isz = 8;
                } else {
                    isz = p.opsz66 ? 2 : 4;
                }
            } else {
                isz = opsize < 4 ? opsize : 4;
                // A 32-bit immediate sign-extends into 64-bit operand
                // size (81/C7/69/68 families); only mov r32, imm32
                // zero-extends, and that path keeps opsize at 4.
                if (isz == 4 && opsize == 8) {
                    sext = true;
                }
            }
            break;
        default:
            break;
        }
        if (!st.has(isz)) {
            return std::unexpected(
                DecodeError{.kind = DecodeError::Kind::kTruncated, .offset = st.pos});
        }
        uint64_t v = 0;
        const uint8_t* ip = st.bytes.data() + st.pos;
        if (isz == 8) {
            v = kernel::get_le64(ip);
        } else if (isz == 4) {
            v = kernel::get_le32(ip);
        } else if (isz == 2) {
            v = kernel::get_le16(ip);
        } else {
            v = ip[0];
        }
        st.pos += isz;
        if (sext) {
            if (isz == 1 && (v & 0x80U) != 0U) {
                v |= 0xFFFFFFFFFFFFFF00ULL;
            }
            if (isz == 4 && (v & 0x80000000ULL) != 0) {
                v |= 0xFFFFFFFF00000000ULL;
            }
        }
        Operand& dst = out.ops[ent.imm_slot];
        dst.disp = static_cast<int64_t>(v);
        if (dst.kind != Operand::Kind::kMem) {
            dst.kind = Operand::Kind::kImm;
            dst.size = isz;
        }
    }

    out.length = static_cast<uint8_t>(st.pos);
    return out;
}

} // namespace micro_forge::arch::x86_64
