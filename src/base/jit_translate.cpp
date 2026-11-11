/**
 * @file jit_translate.cpp
 * @brief Copy-and-patch lowering for the first instruction slice.
 * @version 0.1.0
 */

#include "micro_forge/base/jit_translate.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace micro_forge::base {

namespace {

using arch::x86_64::Mnemonic;
using arch::x86_64::Operand;
using arch::x86_64::Reg;

// --- templates ---------------------------------------------------------
// All templates operate on the guest register file through RDI, with the
// register slot carried in disp32. SIB 0x27 = scale 1, index none, base rdi.
// movabs rax,imm64 ; mov [rdi+slot32],rax. Holes: imm64@2, slot32@14.
// A full immediate permits both zero-extended dwords and sign-extended qwords.
constexpr std::array<std::uint8_t, 18> kMovSlotImm = {
    0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0x48, 0x89, 0x84, 0x27, 0, 0, 0, 0,
};
// add qword [rdi+disp32], imm32                 (48 81 84 27 <disp32> <imm32>)
constexpr std::array<std::uint8_t, 12> kAddSlotImm = {
    0x48, 0x81, 0x84, 0x27, 0, 0, 0, 0, 0, 0, 0, 0,
};
// sub qword [rdi+disp32], imm32                 (48 81 AC 27 <disp32> <imm32>)
constexpr std::array<std::uint8_t, 12> kSubSlotImm = {
    0x48, 0x81, 0xAC, 0x27, 0, 0, 0, 0, 0, 0, 0, 0,
};
// and qword [rdi+disp32], imm32                 (48 81 A4 27 <disp32> <imm32>)
constexpr std::array<std::uint8_t, 12> kAndSlotImm = {
    0x48, 0x81, 0xA4, 0x27, 0, 0, 0, 0, 0, 0, 0, 0,
};
// or qword [rdi+disp32], imm32                  (48 81 8C 27 <disp32> <imm32>)
constexpr std::array<std::uint8_t, 12> kOrSlotImm = {
    0x48, 0x81, 0x8C, 0x27, 0, 0, 0, 0, 0, 0, 0, 0,
};
// xor qword [rdi+disp32], imm32                 (48 81 B4 27 <disp32> <imm32>)
constexpr std::array<std::uint8_t, 12> kXorSlotImm = {
    0x48, 0x81, 0xB4, 0x27, 0, 0, 0, 0, 0, 0, 0, 0,
};
// mov rax, [rdi+src] ; mov [rdi+dst], rax   (reg-to-reg slot copy)
// 48 8B 84 27 <src32>  48 89 84 27 <dst32>
constexpr std::array<std::uint8_t, 16> kMovSlotSlot = {
    0x48, 0x8B, 0x84, 0x27, 0, 0, 0, 0, 0x48, 0x89, 0x84, 0x27, 0, 0, 0, 0,
};
// Holes: src32@4, dst32@12.
// lea dst,[base+disp]: mov rax,[rdi+base] ; add rax,disp32 ; mov [rdi+dst],rax
// 48 8B 84 27 <base32>  48 05 <disp32>  48 89 84 27 <dst32>
constexpr std::array<std::uint8_t, 22> kLeaSlot = {
    0x48, 0x8B, 0x84, 0x27, 0, 0, 0, 0, 0x48, 0x05, 0, 0, 0, 0, 0x48, 0x89, 0x84, 0x27, 0, 0, 0, 0,
};
// Holes: base32@4, disp32@10, dst32@18 (the final mov starts at 14).
// mov dst,[base+disp] via helper:
//   push rdi ; mov rsi,[rdi+base] ; add rsi,disp32 ; mov rdi,<cpu64> ;
//   mov edx,width32 ; movabs r11,jit_load ; call r11 ; pop rdi ; mov [rdi+dst],rax
constexpr std::array<std::uint8_t, 53> kLoadSlot = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @1: mov rsi,[rdi+base32] (8)
    0x48, 0x81, 0xC6, 0,    0, 0, 0,          // @9: add rsi,disp32 (7)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @16: mov rdi,cpu64 (10)
    0xBA, 0,    0,    0,    0,                // @26: mov edx,width32 (5)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @31: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @41: call r11 (3)
    0x5F,                                     // @44: pop rdi (1)
    0x48, 0x89, 0x84, 0x27, 0, 0, 0, 0,       // @45: mov [rdi+dst32],rax (8)
};
// Holes: base32@5, disp32@12, cpu64@18, width32@27, helper64@33, dst32@49.
// Indexed load: cumulative starts 0+1+8+8+4+3+7+10+5+10+3+1+8 = 68.
// REX.R (4C, not 48) selects r8 for the index slot load.
constexpr std::array<std::uint8_t, 68> kLoadIdxSlot = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @1: mov rsi,[rdi+base32] (8)
    0x4C, 0x8B, 0x84, 0x27, 0, 0, 0, 0,       // @9: mov r8,[rdi+idx32] (8)
    0x49, 0xC1, 0xE0, 0,                      // @17: shl r8,shift8 (4)
    0x4C, 0x01, 0xC6,                         // @21: add rsi,r8 (3)
    0x48, 0x81, 0xC6, 0,    0, 0, 0,          // @24: add rsi,disp32 (7)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @31: mov rdi,cpu64 (10)
    0xBA, 0,    0,    0,    0,                // @41: mov edx,width32 (5)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @46: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @56: call r11 (3)
    0x5F,                                     // @59: pop rdi (1)
    0x48, 0x89, 0x84, 0x27, 0, 0, 0, 0,       // @60: mov [rdi+dst32],rax (8)
};
// Holes: base32@5, idx32@13, shift8@20, disp32@27, cpu64@33,
// width32@42, helper64@48, dst32@64; later holes shift by 8+4+3 = 15.
// mov dst,[rip+disp] (absolute vaddr precomputed) via helper:
//   push rdi ; mov rsi,<abs64> ; mov rdi,<cpu64> ; mov edx,width32 ;
//   movabs r11,jit_load ; call r11 ; pop rdi ; mov [rdi+dst],rax
constexpr std::array<std::uint8_t, 48> kLoadRip = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0xBE, 0,    0,    0, 0, 0, 0, 0, 0, // @1: mov rsi,abs64 (10)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @11: mov rdi,cpu64 (10)
    0xBA, 0,    0,    0,    0,                // @21: mov edx,width32 (5)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @26: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @36: call r11 (3)
    0x5F,                                     // @39: pop rdi (1)
    0x48, 0x89, 0x84, 0x27, 0, 0, 0, 0,       // @40: mov [rdi+dst32],rax (8)
};
// Holes: abs64@3, cpu64@13, width32@22, helper64@28, dst32@44.
// mov [base+disp],src via helper:
//   push rdi ; mov rcx,[rdi+src] ; mov rsi,[rdi+base] ; add rsi,disp32 ;
//   mov rdi,<cpu64> ; mov edx,width32 ; movabs r11,jit_store ; call r11 ; pop rdi
// ABI: rdi=cpu, rsi=vaddr, edx=width, rcx=value.
constexpr std::array<std::uint8_t, 53> kStoreSlot = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0x8B, 0x8C, 0x27, 0, 0, 0, 0,       // @1: mov rcx,[rdi+src32] (8)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @9: mov rsi,[rdi+base32] (8)
    0x48, 0x81, 0xC6, 0,    0, 0, 0,          // @17: add rsi,disp32 (7)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @24: mov rdi,cpu64 (10)
    0xBA, 0,    0,    0,    0,                // @34: mov edx,width32 (5)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @39: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @49: call r11 (3)
    0x5F,                                     // @52: pop rdi (1)
};
// Holes: src32@5, base32@13, disp32@20, cpu64@26, width32@35,
// helper64@41.
// Indexed store: cumulative starts 0+1+8+8+8+4+3+7+10+5+10+3+1 = 68.
constexpr std::array<std::uint8_t, 68> kStoreIdxSlot = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0x8B, 0x8C, 0x27, 0, 0, 0, 0,       // @1: mov rcx,[rdi+src32] (8)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @9: mov rsi,[rdi+base32] (8)
    0x4C, 0x8B, 0x84, 0x27, 0, 0, 0, 0,       // @17: mov r8,[rdi+idx32] (8)
    0x49, 0xC1, 0xE0, 0,                      // @25: shl r8,shift8 (4)
    0x4C, 0x01, 0xC6,                         // @29: add rsi,r8 (3)
    0x48, 0x81, 0xC6, 0,    0, 0, 0,          // @32: add rsi,disp32 (7)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @39: mov rdi,cpu64 (10)
    0xBA, 0,    0,    0,    0,                // @49: mov edx,width32 (5)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @54: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @64: call r11 (3)
    0x5F,                                     // @67: pop rdi (1)
};
// Holes: src32@5, base32@13, idx32@21, shift8@28, disp32@35,
// cpu64@41, width32@50, helper64@56; later holes shift by 8+4+3 = 15.
// add qword [base+disp],src via helper (rdi=cpu, rsi=vaddr, rdx=value):
//   push rdi ; mov rdx,[rdi+src] ; mov rsi,[rdi+base] ; add rsi,disp32 ;
//   mov rdi,<cpu64> ; movabs r11,jit_add_mem ; call r11 ; pop rdi
constexpr std::array<std::uint8_t, 48> kAddMemSlot = {
    0x57,                                     // @0: push rdi (1)
    0x48, 0x8B, 0x94, 0x27, 0, 0, 0, 0,       // @1: mov rdx,[rdi+src32] (8)
    0x48, 0x8B, 0xB4, 0x27, 0, 0, 0, 0,       // @9: mov rsi,[rdi+base32] (8)
    0x48, 0x81, 0xC6, 0,    0, 0, 0,          // @17: add rsi,disp32 (7)
    0x48, 0xBF, 0,    0,    0, 0, 0, 0, 0, 0, // @24: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0,    0, 0, 0, 0, 0, 0, // @34: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                         // @44: call r11 (3)
    0x5F,                                     // @47: pop rdi (1)
};
// Data holes: src32@5, base32@13, disp32@20 (four bytes each).
// Relocations: cpu64@26 (8 bytes), helper64@36 (8 bytes).
// add reg,imm via helper (helper updates the value and flags):
//   push rdi ; mov esi,<slot32> ; mov edx,<imm32> ; mov ecx,<width32> ; mov rdi,<cpu64> ;
//   movabs r11,jit_add ; call r11 ; pop rdi
constexpr std::array<std::uint8_t, 40> kAddImmHelper = {
    0x57,                                  // @0: push rdi (1)
    0xBE, 0,    0,    0, 0,                // @1: mov esi,slot32 (5)
    0xBA, 0,    0,    0, 0,                // @6: mov edx,imm32 (5)
    0xB9, 0,    0,    0, 0,                // @11: mov ecx,width32 (5)
    0x48, 0xBF, 0,    0, 0, 0, 0, 0, 0, 0, // @16: mov rdi,cpu64 (10)
    0x49, 0xBB, 0,    0, 0, 0, 0, 0, 0, 0, // @26: movabs r11,helper64 (10)
    0x41, 0xFF, 0xD3,                      // @36: call r11 (3)
    0x5F,                                  // @39: pop rdi (1)
};
// Holes: slot32@2, imm32@7, width32@12, cpu64@18, helper64@28.
// ret
constexpr std::array<std::uint8_t, 1> kRet = {
    0xC3,
};

// Keep the final slot write eight bytes long, preserving every existing hole.
template <auto Code>
std::span<const std::uint8_t> load_code(unsigned width) {
    static constexpr auto byte = [] {
        auto code = Code;
        code[code.size() - 8] = 0x40; // REX ; mov byte [rdi+dst32],al
        code[code.size() - 7] = 0x88;
        return code;
    }();
    static constexpr auto word = [] {
        auto code = Code;
        code[code.size() - 8] = 0x66; // mov word [rdi+dst32],ax
        return code;
    }();
    static constexpr auto wide = Code; // dword loads zero-extend into a full slot
    if (width == 1) {
        return byte;
    }
    if (width == 2) {
        return word;
    }
    return wide;
}

bool memory_width(unsigned width) {
    return width == 1 || width == 2 || width == 4 || width == 8;
}

std::optional<std::size_t> operand_slot(const Operand& op, bool rex) {
    auto slot = reg_slot_offset(op.reg);
    if (slot && op.size == 1 && !rex && *slot >= 32 && *slot <= 56) {
        *slot = *slot - 32 + 1; // AH/CH/DH/BH: byte 1 of RAX/RCX/RDX/RBX
    }
    return slot;
}

} // namespace

bool TranslatedInsn::has_valid_layout() const {
    // Calls and Cpu* relocations come as a pair and must name a helper.
    if (code.empty() || call_offset.has_value() != cpu_offset.has_value() ||
        call_offset.has_value() != (helper != JitHelper::kNone)) {
        return false;
    }
    if (call_offset && (*call_offset < 2 || *call_offset > code.size() ||
                        code.size() - *call_offset < 11 || code[*call_offset - 2] != 0x49 ||
                        code[*call_offset - 1] != 0xBB || code[*call_offset + 8] != 0x41 ||
                        code[*call_offset + 9] != 0xFF || code[*call_offset + 10] != 0xD3)) {
        return false;
    }
    if (cpu_offset &&
        (*cpu_offset < 2 || *cpu_offset > code.size() || code.size() - *cpu_offset < 8 ||
         code[*cpu_offset - 2] != 0x48 || code[*cpu_offset - 1] != 0xBF)) {
        return false;
    }
    std::vector<bool> occupied(code.size(), false);
    const auto hole = [&](std::size_t offset, std::size_t size) {
        if (size == 0 || size > 8 || offset > code.size() || code.size() - offset < size) {
            return false;
        }
        for (std::size_t i = offset; i < offset + size; ++i) {
            if (code[i] != 0 || occupied[i]) {
                return false;
            }
            occupied[i] = true;
        }
        return true;
    };
    if (call_offset && (!hole(*call_offset, 8) || !hole(*cpu_offset, 8))) {
        return false;
    }
    return std::ranges::all_of(
        patches, [&hole](const JitPage::Patch& patch) { return hole(patch.offset, patch.size); });
}

std::optional<std::size_t> reg_slot_offset(Reg reg) {
    const auto value = static_cast<std::uint8_t>(reg);
    if (value >= 16) {
        return std::nullopt; // not a general-purpose register
    }
    return static_cast<std::size_t>(value) * 8U;
}

std::optional<TranslatedInsn> translate_insn(const arch::x86_64::DecodedInsn& insn, uint64_t rip) {
    // ret is deliberately not lowered: the JIT template returns to the
    // dispatcher, while a guest ret pops [rsp] into rip. Ending the block
    // just before it lets the interpreter perform the real ret.
    if (insn.mnemonic == Mnemonic::kRet) {
        return std::nullopt;
    }
    // Only the qword, unsegmented base+disp form fits this helper ABI.
    if (insn.mnemonic == Mnemonic::kAdd && insn.ops[0].kind == Operand::Kind::kMem &&
        insn.ops[1].kind == Operand::Kind::kReg && insn.ops[0].size == 8 && insn.ops[1].size == 8 &&
        insn.ops[0].has_base && !insn.ops[0].has_index && !insn.ops[0].rip_relative &&
        insn.addr_size == 8 && !insn.has_lock && insn.seg != arch::x86_64::Seg::kFs &&
        insn.seg != arch::x86_64::Seg::kGs && insn.ops[0].disp >= INT32_MIN &&
        insn.ops[0].disp <= INT32_MAX) {
        const auto base = reg_slot_offset(insn.ops[0].base);
        const auto source = reg_slot_offset(insn.ops[1].reg);
        if (!base || !source) {
            return std::nullopt;
        }
        return TranslatedInsn{
            .code = kAddMemSlot,
            .patches =
                {
                    JitPage::Patch{.offset = 5, .size = 4, .value = *source},
                    JitPage::Patch{.offset = 13, .size = 4, .value = *base},
                    JitPage::Patch{
                        .offset = 20,
                        .size = 4,
                        .value = static_cast<std::uint32_t>(insn.ops[0].disp),
                    },
                },
            .call_offset = 36,
            .cpu_offset = 26,
            .helper = JitHelper::kAddMem,
        };
    }
    // mov [base+index*scale+disp],reg: compute vaddr before calling the helper.
    if (insn.mnemonic == Mnemonic::kMov && insn.ops[0].kind == Operand::Kind::kMem &&
        insn.ops[1].kind == Operand::Kind::kReg && insn.ops[0].has_base &&
        !insn.ops[0].rip_relative && memory_width(insn.ops[0].size) &&
        insn.ops[1].size == insn.ops[0].size && insn.addr_size == 8 && !insn.has_lock &&
        insn.seg != arch::x86_64::Seg::kFs && insn.seg != arch::x86_64::Seg::kGs &&
        insn.ops[0].disp >= INT32_MIN && insn.ops[0].disp <= INT32_MAX) {
        const auto base = reg_slot_offset(insn.ops[0].base);
        const auto source = operand_slot(insn.ops[1], insn.rex_present);
        if (!base.has_value() || !source.has_value()) {
            return std::nullopt;
        }
        const auto& mem = insn.ops[0];
        if (mem.has_index) {
            const auto index = reg_slot_offset(mem.index);
            if (!index || mem.scale > 8 || !std::has_single_bit(mem.scale)) {
                return std::nullopt;
            }
            return TranslatedInsn{
                .code = kStoreIdxSlot,
                .patches =
                    {
                        {.offset = 5, .size = 4, .value = *source},
                        {.offset = 13, .size = 4, .value = *base},
                        {.offset = 21, .size = 4, .value = *index},
                        {
                            .offset = 28,
                            .size = 1,
                            .value = static_cast<unsigned>(std::countr_zero(mem.scale)),
                        },
                        {.offset = 35, .size = 4, .value = static_cast<std::uint32_t>(mem.disp)},
                        {.offset = 50, .size = 4, .value = mem.size},
                    },
                .call_offset = 56,
                .cpu_offset = 41,
                .helper = JitHelper::kStore,
            };
        }
        std::vector<JitPage::Patch> patches = {
            JitPage::Patch{
                .offset = 5,
                .size = 4,
                .value = *source,
            },
            JitPage::Patch{
                .offset = 13,
                .size = 4,
                .value = *base,
            },
            JitPage::Patch{
                .offset = 20,
                .size = 4,
                .value = static_cast<std::uint32_t>(insn.ops[0].disp),
            },
            JitPage::Patch{
                .offset = 35,
                .size = 4,
                .value = mem.size,
            }, // width
        };
        return TranslatedInsn{
            .code = kStoreSlot,
            .patches = std::move(patches),
            .call_offset = 41,
            .cpu_offset = 26,
            .helper = JitHelper::kStore,
        };
    }
    // Stores have a memory destination; only the remaining forms share
    // the register-destination lookup.
    if (insn.ops[0].kind != Operand::Kind::kReg || !memory_width(insn.ops[0].size)) {
        return std::nullopt;
    }
    const auto slot = operand_slot(insn.ops[0], insn.rex_present);
    if (!slot.has_value()) {
        return std::nullopt;
    }
    // mov reg,[base+index*scale+disp]: compute vaddr before calling the helper.
    if (insn.mnemonic == Mnemonic::kMov && insn.ops[0].kind == Operand::Kind::kReg &&
        insn.ops[1].kind == Operand::Kind::kMem && insn.ops[1].has_base &&
        !insn.ops[1].rip_relative && insn.ops[0].size == insn.ops[1].size && insn.addr_size == 8 &&
        !insn.has_lock && insn.seg != arch::x86_64::Seg::kFs &&
        insn.seg != arch::x86_64::Seg::kGs && insn.ops[1].disp >= INT32_MIN &&
        insn.ops[1].disp <= INT32_MAX) {
        const auto base = reg_slot_offset(insn.ops[1].base);
        if (!base.has_value()) {
            return std::nullopt;
        }
        const auto& mem = insn.ops[1];
        if (mem.has_index) {
            const auto index = reg_slot_offset(mem.index);
            if (!index || mem.scale > 8 || !std::has_single_bit(mem.scale)) {
                return std::nullopt;
            }
            return TranslatedInsn{
                .code = load_code<kLoadIdxSlot>(mem.size),
                .patches =
                    {
                        {.offset = 5, .size = 4, .value = *base},
                        {.offset = 13, .size = 4, .value = *index},
                        {
                            .offset = 20,
                            .size = 1,
                            .value = static_cast<unsigned>(std::countr_zero(mem.scale)),
                        },
                        {.offset = 27, .size = 4, .value = static_cast<std::uint32_t>(mem.disp)},
                        {.offset = 42, .size = 4, .value = mem.size},
                        {.offset = 64, .size = 4, .value = *slot},
                    },
                .call_offset = 48,
                .cpu_offset = 33,
                .helper = JitHelper::kLoad,
            };
        }
        std::vector<JitPage::Patch> patches = {
            JitPage::Patch{
                .offset = 5,
                .size = 4,
                .value = *base,
            },
            JitPage::Patch{
                .offset = 12,
                .size = 4,
                .value = static_cast<std::uint32_t>(insn.ops[1].disp),
            },
            JitPage::Patch{
                .offset = 27,
                .size = 4,
                .value = mem.size,
            }, // width
            JitPage::Patch{
                .offset = 49,
                .size = 4,
                .value = *slot,
            },
        };
        // cpu64 (offset 18) and helper64 (offset 33) are filled by the compiler.
        return TranslatedInsn{
            .code = load_code<kLoadSlot>(mem.size),
            .patches = std::move(patches),
            .call_offset = 33,
            .cpu_offset = 18,
            .helper = JitHelper::kLoad,
        };
    }
    // mov reg,[rip+disp]: the absolute address is known at compile time
    // (rip_after + disp), so the helper receives it directly.
    if (insn.mnemonic == Mnemonic::kMov && insn.ops[0].kind == Operand::Kind::kReg &&
        insn.ops[1].kind == Operand::Kind::kMem && insn.ops[1].rip_relative &&
        insn.ops[0].size == 8) {
        const auto vaddr = rip + insn.length + static_cast<uint64_t>(insn.ops[1].disp);
        std::vector<JitPage::Patch> patches = {
            JitPage::Patch{
                .offset = 3,
                .size = 8,
                .value = vaddr,
            },
            JitPage::Patch{
                .offset = 22,
                .size = 4,
                .value = 8,
            }, // width
            JitPage::Patch{
                .offset = 44,
                .size = 4,
                .value = *slot,
            },
        };
        return TranslatedInsn{
            .code = kLoadRip,
            .patches = std::move(patches),
            .call_offset = 28,
            .cpu_offset = 13,
            .helper = JitHelper::kLoad,
        };
    }
    // add reg,imm32: helper updates the value and flags together.
    if (insn.mnemonic == Mnemonic::kAdd && insn.ops[0].kind == Operand::Kind::kReg &&
        insn.ops[1].kind == Operand::Kind::kImm &&
        (insn.ops[0].size == 4 || insn.ops[0].size == 8) && insn.ops[1].disp >= INT32_MIN &&
        insn.ops[1].disp <=
            (insn.ops[0].size == 4 ? static_cast<int64_t>(UINT32_MAX) : INT32_MAX)) {
        std::vector<JitPage::Patch> patches = {
            JitPage::Patch{
                .offset = 2,
                .size = 4,
                .value = *slot,
            },
            JitPage::Patch{
                .offset = 7,
                .size = 4,
                .value = static_cast<std::uint32_t>(insn.ops[1].disp),
            },
            {.offset = 12, .size = 4, .value = insn.ops[0].size},
        };
        return TranslatedInsn{
            .code = kAddImmHelper,
            .patches = std::move(patches),
            .call_offset = 28,
            .cpu_offset = 18,
            .helper = JitHelper::kAdd,
        };
    }
    // lea with a base register and a displacement (no index/scale).
    if (insn.mnemonic == Mnemonic::kLea && insn.ops[1].kind == Operand::Kind::kMem &&
        insn.ops[1].has_base && !insn.ops[1].has_index && !insn.ops[1].rip_relative &&
        insn.ops[0].size == 8) {
        const auto base = reg_slot_offset(insn.ops[1].base);
        if (!base.has_value()) {
            return std::nullopt;
        }
        std::vector<JitPage::Patch> patches = {
            JitPage::Patch{.offset = 4, .size = 4, .value = *base},
            JitPage::Patch{
                .offset = 10,
                .size = 4,
                .value = static_cast<std::uint32_t>(insn.ops[1].disp),
            },
            JitPage::Patch{
                .offset = 18,
                .size = 4,
                .value = *slot,
            },
        };
        return TranslatedInsn{
            .code = kLeaSlot,
            .patches = std::move(patches),
            .call_offset = std::nullopt,
            .cpu_offset = std::nullopt,
        };
    }
    // Register-to-register move: copy one slot to another.
    if (insn.mnemonic == Mnemonic::kMov && insn.ops[1].kind == Operand::Kind::kReg &&
        insn.ops[0].size == 8) {
        const auto source = reg_slot_offset(insn.ops[1].reg);
        if (!source.has_value()) {
            return std::nullopt;
        }
        std::vector<JitPage::Patch> patches = {
            JitPage::Patch{
                .offset = 4,
                .size = 4,
                .value = *source,
            },
            JitPage::Patch{
                .offset = 12,
                .size = 4,
                .value = *slot,
            },
        };
        return TranslatedInsn{
            .code = kMovSlotSlot,
            .patches = std::move(patches),
            .call_offset = std::nullopt,
            .cpu_offset = std::nullopt,
        };
    }
    // Only guest imm32 forms are lowered; wider constants (kernel high-half
    // addresses) stay with the interpreter rather than being truncated.
    if ((insn.ops[0].size != 4 && insn.ops[0].size != 8) ||
        insn.ops[1].kind != Operand::Kind::kImm || insn.ops[1].disp < INT32_MIN ||
        insn.ops[1].disp > (insn.ops[0].size == 4 ? static_cast<int64_t>(UINT32_MAX) : INT32_MAX)) {
        return std::nullopt;
    }
    const auto imm = insn.ops[0].size == 4 ? static_cast<std::uint32_t>(insn.ops[1].disp)
                                           : static_cast<uint64_t>(insn.ops[1].disp);
    if (insn.mnemonic == Mnemonic::kMov) {
        return TranslatedInsn{
            .code = kMovSlotImm,
            .patches =
                {
                    {.offset = 2, .size = 8, .value = imm},
                    {.offset = 14, .size = 4, .value = *slot},
                },
            .call_offset = std::nullopt,
            .cpu_offset = std::nullopt,
        };
    }
    // Arithmetic would have to update guest flags; the templates do not
    // model them yet, so those instructions stay with the interpreter.
    return std::nullopt;
}

} // namespace micro_forge::base
