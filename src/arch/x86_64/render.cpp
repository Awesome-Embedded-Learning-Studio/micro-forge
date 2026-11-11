/**
 * @file render.cpp
 * @brief Intel-syntax text rendering for decoded instructions.
 * @version 0.1.0
 */

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>

#include "micro_forge/arch/x86_64/decoder.hpp"
#include "micro_forge/kernel/bit.hpp"

namespace micro_forge::arch::x86_64 {
namespace {

constexpr std::array<std::string_view, 16> kCcName = {
    "o", "no", "b", "ae", "e", "ne", "be", "a", "s", "ns", "p", "np", "l", "ge", "le", "g",
};

constexpr std::array<std::string_view, 16> kGpr64 = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
};
constexpr std::array<std::string_view, 16> kGpr32 = {
    "eax", "ecx", "edx",  "ebx",  "esp",  "ebp",  "esi",  "edi",
    "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d",
};
constexpr std::array<std::string_view, 16> kGpr16 = {
    "ax",  "cx",  "dx",   "bx",   "sp",   "bp",   "si",   "di",
    "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w",
};
constexpr std::array<std::string_view, 16> kGpr8Rex = {
    "al",  "cl",  "dl",   "bl",   "spl",  "bpl",  "sil",  "dil",
    "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b",
};
constexpr std::array<std::string_view, 16> kGpr8NoRex = {
    "al",  "cl",  "dl",   "bl",   "ah",   "ch",   "dh",   "bh",
    "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b",
};

void append_reg(std::string& s, Reg reg, uint8_t size, bool rex) {
    const auto code = static_cast<unsigned>(reg);
    if (code >= 16 && code <= 31) {
        s += "xmm";
        s += std::to_string(code - 16);
        return;
    }
    if (code >= 32 && code <= 41) {
        switch (reg) {
        case Reg::kCr0:
            s += "cr0";
            return;
        case Reg::kCr2:
            s += "cr2";
            return;
        case Reg::kCr3:
            s += "cr3";
            return;
        case Reg::kCr4:
            s += "cr4";
            return;
        case Reg::kDr0:
            s += "dr0";
            return;
        case Reg::kDr1:
            s += "dr1";
            return;
        case Reg::kDr2:
            s += "dr2";
            return;
        case Reg::kDr3:
            s += "dr3";
            return;
        case Reg::kDr6:
            s += "dr6";
            return;
        default:
            s += "dr7";
            return;
        }
    }
    switch (reg) {
    case Reg::kEs:
        s += "es";
        return;
    case Reg::kCs:
        s += "cs";
        return;
    case Reg::kSs:
        s += "ss";
        return;
    case Reg::kDs:
        s += "ds";
        return;
    case Reg::kFs:
        s += "fs";
        return;
    case Reg::kGs:
        s += "gs";
        return;
    default:
        break;
    }
    const unsigned idx = static_cast<unsigned>(code) & 15U;
    switch (size) {
    case 1:
        s += (rex || idx >= 8) ? kGpr8Rex[idx] : kGpr8NoRex[idx];
        break;
    case 2:
        s += kGpr16[idx];
        break;
    case 4:
        s += kGpr32[idx];
        break;
    default:
        s += kGpr64[idx];
        break;
    }
}

void append_hex(std::string& s, uint64_t v) {
    s += "0x";
    static constexpr char k_digits[] = "0123456789abcdef";
    if (v == 0) {
        s += '0';
        return;
    }
    char tmp[17];
    int i = 16;
    while (v != 0) {
        tmp[--i] = k_digits[v & 0xFU];
        v >>= 4U;
    }
    s.append(tmp + i, static_cast<size_t>(16 - i));
}

void append_seg(std::string& s, Seg seg) {
    switch (seg) {
    case Seg::kEs:
        s += "es:";
        break;
    case Seg::kCs:
        s += "cs:";
        break;
    case Seg::kSs:
        s += "ss:";
        break;
    case Seg::kDs:
        s += "ds:";
        break;
    case Seg::kFs:
        s += "fs:";
        break;
    case Seg::kGs:
        s += "gs:";
        break;
    default:
        break;
    }
}

void append_mem(std::string& s, const Operand& m, bool with_width, unsigned addr_size) {
    if (with_width) {
        switch (m.size) {
        case 1:
            s += "BYTE PTR ";
            break;
        case 2:
            s += "WORD PTR ";
            break;
        case 4:
            s += "DWORD PTR ";
            break;
        case 8:
            s += "QWORD PTR ";
            break;
        case 16:
            s += "XMMWORD PTR ";
            break;
        default:
            break;
        }
    }
    s += '[';
    bool first = true;
    if (m.rip_relative) {
        s += "rip";
        first = false;
    }
    if (m.has_base) {
        s += (addr_size == 4 ? kGpr32 : kGpr64)[static_cast<unsigned>(m.base) & 15U];
        first = false;
    }
    if (m.has_index) {
        if (!first) {
            s += '+';
        }
        s += (addr_size == 4 ? kGpr32 : kGpr64)[static_cast<unsigned>(m.index) & 15U];
        s += '*';
        s += std::to_string(m.scale); // objdump prints decimal, "*1" kept
        first = false;
    }
    if (m.disp != 0) {
        if (m.rip_relative && m.disp < 0) {
            // binutils prints raw unsigned disp32 zero-extended for RIP.
            s += '+';
            append_hex(s, static_cast<uint64_t>(m.disp));
        } else {
            if (!first && m.disp > 0) {
                s += '+';
            } else if (m.disp < 0) {
                s += '-';
            }
            append_hex(s,
                       m.disp < 0 ? static_cast<uint64_t>(-m.disp) : static_cast<uint64_t>(m.disp));
        }
    } else if (m.has_disp) {
        if (!first) {
            s += '+';
        }
        s += "0x0"; // displacement present but zero (mod 1/2, SIB 101)
    } else if (first) {
        // Absolute [0x0] with no base/index.
        s += "0x0";
    }
    s += ']';
}

} // namespace

std::string_view gpr_name(Reg reg, uint8_t size, bool rex_present) {
    const unsigned code = static_cast<unsigned>(reg) & 15U;
    switch (size) {
    case 1:
        return (rex_present || code >= 8) ? kGpr8Rex[code] : kGpr8NoRex[code];
    case 2:
        return kGpr16[code];
    case 4:
        return kGpr32[code];
    default:
        return kGpr64[code];
    }
}

/// @brief Mnemonic text indexed by Mnemonic value (order = enum order).
constexpr std::array<std::string_view, 179> kMnemonicNames = {
    "mov",        "movsx",     "movzx",     "movsxd",    "xchg",       "lea",       "push",
    "pop",        "cmovcc",    "movabs",    "add",       "adc",        "sub",       "sbb",
    "inc",        "dec",       "neg",       "cmp",       "mul",        "imul",      "div",
    "idiv",       "and",       "or",        "xor",       "not",        "test",      "shl",
    "shr",        "sar",       "rol",       "ror",       "rcl",        "rcr",       "shld",
    "shrd",       "bt",        "bts",       "btr",       "btc",        "bsf",       "bsr",
    "xadd",       "cmpxchg",   "jmp",       "jcc",       "call",       "ret",       "leave",
    "loop",       "loope",     "loopne",    "jrcxz",     "setcc",      "cwde",      "cdqe",
    "cdq",        "cqo",       "nop",       "endbr64",   "pause",      "lfence",    "mfence",
    "sfence",     "movsb",     "movsw",     "movsd",     "movsq",      "stosb",     "stosw",
    "stosd",      "stosq",     "lodsb",     "lodsw",     "lodsd",      "lodsq",     "scasb",
    "scasw",      "scasd",     "scasq",     "cmpsb",     "cmpsw",      "cmpsd",     "cmpsq",
    "cpuid",      "rdtsc",     "rdmsr",     "wrmsr",     "rdrand",     "psrldq",    "pslldq",
    "shufps",     "movlps",    "movhps",    "movlpd",    "movlhps",    "movhlps",   "pmovmskb",
    "syscall",    "sysretq",   "iret",      "lret",      "sldt",       "str",       "lldt",
    "ltr",        "swapgs",    "invlpg",    "lgdt",      "lidt",       "sgdt",      "sidt",
    "hlt",        "pushf",     "popf",      "cli",       "sti",        "in",        "out",
    "cld",        "std",       "bswap",     "tzcnt",     "lzcnt",      "fxsave",    "fxrstor",
    "fxsave64",   "fxrstor64", "movaps",    "movups",    "movdqa",     "movdqu",    "movq",
    "movd",       "pshufd",    "pshuflw",   "pshufhw",   "punpcklbw",  "punpcklwd", "punpckldq",
    "punpcklqdq", "punpckhbw", "punpckhwd", "punpckhdq", "punpckhqdq", "packuswb",  "pxor",
    "pand",       "pandn",     "por",       "psrlw",     "psrld",      "psrlq",     "psllw",
    "pslld",      "psllq",     "paddd",     "paddq",     "psubd",      "paddb",     "psubb",
    "paddw",      "psubw",     "psubq",     "pminub",    "pmaxub",     "pminsw",    "pmaxsw",
    "rdsspq",     "pcmpeqb",   "pcmpeqd",   "pcmpeqq",   "psadbw",     "pextrw",    "movss",
    "clac",       "stac",      "fninit",    "ud2",
};
static_assert(kMnemonicNames.size() == 179, "keep in sync with Mnemonic in decoded.hpp");

std::string_view mnemonic_name(Mnemonic mn) {
    const auto idx = static_cast<size_t>(mn);
    if (idx >= kMnemonicNames.size()) {
        std::abort(); // unreachable: table is generated from the enum
    }
    return kMnemonicNames[idx];
}

std::string to_text(const DecodedInsn& insn, uint64_t rip) {
    std::string s;
    // rep prefixes only carry meaning on string instructions; elsewhere
    // they are bnd-style padding that objdump renders differently.
    const bool is_string = [](Mnemonic m) {
        switch (m) {
        case Mnemonic::kMovsb:
        case Mnemonic::kMovsw:
        case Mnemonic::kMovsd:
        case Mnemonic::kMovsq:
        case Mnemonic::kStosb:
        case Mnemonic::kStosw:
        case Mnemonic::kStosd:
        case Mnemonic::kStosq:
        case Mnemonic::kLodsb:
        case Mnemonic::kLodsw:
        case Mnemonic::kLodsd:
        case Mnemonic::kLodsq:
        case Mnemonic::kScasb:
        case Mnemonic::kScasw:
        case Mnemonic::kScasd:
        case Mnemonic::kScasq:
        case Mnemonic::kCmpsb:
        case Mnemonic::kCmpsw:
        case Mnemonic::kCmpsd:
        case Mnemonic::kCmpsq:
            return true;
        default:
            return false;
        }
    }(insn.mnemonic);
    if (is_string && insn.rep == Rep::kRep) {
        s += "rep ";
    } else if (is_string && insn.rep == Rep::kRepne) {
        s += "repne ";
    }
    if (insn.has_lock) {
        s += "lock ";
    }
    // objdump spells the 67 prefix explicitly on instructions it cannot
    // change (no memory operand); operands with addressing render the
    // 32-bit registers instead.
    const bool has_mem_operand = std::ranges::any_of(
        insn.ops, [](const Operand& op) { return op.kind == Operand::Kind::kMem; });
    if (insn.addr_size == 4 && !has_mem_operand) {
        s += "addr32 ";
    }

    std::string_view const base = mnemonic_name(insn.mnemonic);
    char buf[12];
    if (insn.mnemonic == Mnemonic::kIretq) {
        s += insn.rex_present ? "iretq" : "iret";
        return s;
    }
    if (insn.mnemonic == Mnemonic::kLretq) {
        s += insn.rex_present ? "retfq" : "retf";
        // The imm16 form (0xCA) prints its stack adjustment.
        if (insn.ops[0].kind == Operand::Kind::kImm) {
            s += ' ';
            append_hex(s, static_cast<uint64_t>(insn.ops[0].disp));
        }
        return s;
    }
    if (insn.mnemonic == Mnemonic::kFninit) {
        // The table slot exists only to consume the ModRM byte; objdump
        // prints the bare mnemonic.
        s += "fninit";
        return s;
    }
    if (insn.mnemonic == Mnemonic::kNop && insn.opsize == 2 && insn.length == 2) {
        // 66 90: objdump keeps the operand size and prints xchg; the
        // one-byte form stays "nop" (new-kernel alignment padding).
        s += "xchg ax, ax";
        return s;
    }
    if (insn.mnemonic == Mnemonic::kSysret) {
        // binutils: 0F 07 is sysretd; REX.W promotes to sysretq.
        s += insn.rex_present ? "sysretq" : "sysretd";
        return s;
    }

    switch (insn.mnemonic) {
    case Mnemonic::kJcc:
    case Mnemonic::kSetcc:
    case Mnemonic::kCmovcc: {
        const std::string_view cc = kCcName[static_cast<unsigned>(insn.cc)];
        std::string_view prefix = "cmov";
        if (insn.mnemonic == Mnemonic::kJcc) {
            prefix = "j";
        } else if (insn.mnemonic == Mnemonic::kSetcc) {
            prefix = "set";
        }
        s += prefix;
        s += cc;
        break;
    }
    default:
        s += base;
        break;
    }
    (void)buf;

    // Branch-with-immediate prints the absolute target.
    const bool branch = (insn.mnemonic == Mnemonic::kJmp || insn.mnemonic == Mnemonic::kJcc ||
                         insn.mnemonic == Mnemonic::kCall || insn.mnemonic == Mnemonic::kLoop ||
                         insn.mnemonic == Mnemonic::kLoope || insn.mnemonic == Mnemonic::kLoopne ||
                         insn.mnemonic == Mnemonic::kJrcxz) &&
                        insn.ops[0].kind == Operand::Kind::kImm;

    bool wrote = false;
    for (const Operand& op : insn.ops) {
        if (op.kind == Operand::Kind::kNone) {
            continue;
        }
        if (wrote) {
            s += ", ";
        } else {
            s += ' ';
        }
        wrote = true;
        switch (op.kind) {
        case Operand::Kind::kReg:
        case Operand::Kind::kImplicit:
            append_reg(s, op.reg, op.size, insn.rex_present);
            break;
        case Operand::Kind::kMem:
            if (!op.has_base && !op.has_index && !op.rip_relative) {
                // moffs / SIB-absolute: objdump prints width + override +
                // bare value ("QWORD PTR gs:0x8").
                if (op.size == 1) {
                    s += "BYTE PTR ";
                } else if (op.size == 2) {
                    s += "WORD PTR ";
                } else if (op.size == 4) {
                    s += "DWORD PTR ";
                } else if (op.size == 8) {
                    s += "QWORD PTR ";
                }
                append_seg(s, insn.seg);
                append_hex(s, static_cast<uint64_t>(op.disp));
                break;
            }
            append_mem(s, op,
                       insn.mnemonic != Mnemonic::kLea && insn.mnemonic != Mnemonic::kLgdt &&
                           insn.mnemonic != Mnemonic::kLidt && insn.mnemonic != Mnemonic::kSgdt &&
                           insn.mnemonic != Mnemonic::kSidt && insn.mnemonic != Mnemonic::kFxsave &&
                           insn.mnemonic != Mnemonic::kFxrstor &&
                           insn.mnemonic != Mnemonic::kFxsave64 &&
                           insn.mnemonic != Mnemonic::kFxrstor64,
                       insn.addr_size);
            break;
        case Operand::Kind::kImm: {
            const bool shift_implicit_one =
                op.size == 0 && op.disp == 1 &&
                (insn.mnemonic == Mnemonic::kShl || insn.mnemonic == Mnemonic::kShr ||
                 insn.mnemonic == Mnemonic::kSar || insn.mnemonic == Mnemonic::kRol ||
                 insn.mnemonic == Mnemonic::kRor || insn.mnemonic == Mnemonic::kRcl ||
                 insn.mnemonic == Mnemonic::kRcr);
            if (shift_implicit_one) {
                s += '1';
                break;
            }
            if (branch) {
                append_hex(s, rip + insn.length + static_cast<uint64_t>(op.disp));
            } else {
                // Mask to operand width, matching objdump's rendering of
                // sign-extended byte immediates ("add eax,0xfffffff0").
                auto v = static_cast<uint64_t>(op.disp);
                if (insn.opsize == 8 && op.size == 4 && (v & 0x80000000ULL) != 0) {
                    // Sign-extended imm32 rendered as 64-bit.
                    v = kernel::bit::sign_extend(v, 32U);
                }
                if (op.size != 8) { // moffs64/imm64 render full-width
                    if (insn.opsize == 4) {
                        v &= 0xFFFFFFFFULL;
                    } else if (insn.opsize == 2) {
                        v &= 0xFFFFULL;
                    } else if (insn.opsize == 1) {
                        v &= 0xFFULL;
                    }
                }
                append_hex(s, v);
            }
            break;
        }
        default:
            break;
        }
    }
    return s;
}

} // namespace micro_forge::arch::x86_64
