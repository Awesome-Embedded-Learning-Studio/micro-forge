/**
 * @file jit_translate_mov.cpp
 * @brief Internal split of jit_translate.cpp: mov-family lowering only.
 */

#include "jit_translate_mov.hpp"

#include "micro_forge/base/jit_templates.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace micro_forge::base::detail {

namespace {

using arch::x86_64::Mnemonic;
using arch::x86_64::Operand;

using namespace jit_templates;

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

} // namespace

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

std::optional<TranslatedInsn> translate_move(const arch::x86_64::DecodedInsn& insn,
                                             std::uint64_t rip) {
    // movzx reg,[base+disp]: jit_load returns the zero-extended value and
    // the untouched kLoadSlot write covers the whole slot, which is exactly
    // movzx semantics at both 32- and 64-bit destinations.
    if (insn.mnemonic == Mnemonic::kMovzx && insn.ops[0].kind == Operand::Kind::kReg &&
        insn.ops[1].kind == Operand::Kind::kMem && insn.ops[1].has_base && !insn.ops[1].has_index &&
        !insn.ops[1].rip_relative && (insn.ops[0].size == 4 || insn.ops[0].size == 8) &&
        (insn.ops[1].size == 1 || insn.ops[1].size == 2) && insn.addr_size == 8 && !insn.has_lock &&
        insn.seg != arch::x86_64::Seg::kFs && insn.seg != arch::x86_64::Seg::kGs &&
        insn.ops[1].disp >= INT32_MIN && insn.ops[1].disp <= INT32_MAX) {
        const auto slot = detail::operand_slot(insn.ops[0], insn.rex_present);
        const auto base = reg_slot_offset(insn.ops[1].base);
        const auto disp = static_cast<std::uint32_t>(insn.ops[1].disp);
        if (slot.has_value() && base.has_value()) {
            return TranslatedInsn{
                .code = jit_templates::kLoadSlot,
                .patches =
                    {
                        {.offset = 5, .size = 4, .value = *base},
                        {.offset = 12, .size = 4, .value = disp},
                        {.offset = 27, .size = 4, .value = insn.ops[1].size},
                        {.offset = 49, .size = 4, .value = *slot},
                    },
                .call_offset = 33,
                .cpu_offset = 18,
                .helper = JitHelper::kLoad,
            };
        }
    }
    // add/adc/sbb/sub/and/or/xor reg,[base+disp]: load then combine; the
    // helper refuses to write back when the load faults.
    if (insn.ops[0].kind == Operand::Kind::kReg && insn.ops[1].kind == Operand::Kind::kMem &&
        insn.ops[1].has_base && !insn.ops[1].has_index && !insn.ops[1].rip_relative &&
        insn.ops[0].size == insn.ops[1].size && insn.addr_size == 8 && !insn.has_lock &&
        insn.seg != arch::x86_64::Seg::kFs && insn.seg != arch::x86_64::Seg::kGs &&
        insn.ops[1].disp >= INT32_MIN && insn.ops[1].disp <= INT32_MAX &&
        (insn.mnemonic == Mnemonic::kAdd || insn.mnemonic == Mnemonic::kAdc ||
         insn.mnemonic == Mnemonic::kSub || insn.mnemonic == Mnemonic::kSbb ||
         insn.mnemonic == Mnemonic::kAnd || insn.mnemonic == Mnemonic::kOr ||
         insn.mnemonic == Mnemonic::kXor)) {
        const auto slot = operand_slot(insn.ops[0], insn.rex_present);
        const auto base = reg_slot_offset(insn.ops[1].base);
        const auto adisp = static_cast<std::uint32_t>(insn.ops[1].disp);
        const auto amn = static_cast<unsigned>(insn.mnemonic);
        if (slot.has_value() && base.has_value()) {
            return TranslatedInsn{
                .code = kAluMemSlot,
                .patches =
                    {
                        {.offset = 2, .size = 4, .value = *slot},
                        {.offset = 9, .size = 4, .value = *base},
                        {.offset = 16, .size = 4, .value = adisp},
                        {.offset = 21, .size = 4, .value = amn},
                        {.offset = 27, .size = 4, .value = insn.ops[0].size},
                    },
                .call_offset = 43,
                .cpu_offset = 33,
                .helper = JitHelper::kAluMem,
            };
        }
    }
    // movzx reg,[base+index*scale+disp]: same whole-slot write trick as the
    // base-only form, through the indexed load template.
    if (insn.mnemonic == Mnemonic::kMovzx && insn.ops[0].kind == Operand::Kind::kReg &&
        insn.ops[1].kind == Operand::Kind::kMem && insn.ops[1].has_base && insn.ops[1].has_index &&
        !insn.ops[1].rip_relative && (insn.ops[0].size == 4 || insn.ops[0].size == 8) &&
        (insn.ops[1].size == 1 || insn.ops[1].size == 2) && insn.addr_size == 8 && !insn.has_lock &&
        insn.seg != arch::x86_64::Seg::kFs && insn.seg != arch::x86_64::Seg::kGs) {
        const auto slot = operand_slot(insn.ops[0], insn.rex_present);
        const auto base = reg_slot_offset(insn.ops[1].base);
        const auto index = reg_slot_offset(insn.ops[1].index);
        const auto& mem = insn.ops[1];
        const auto shift = static_cast<std::uint64_t>(std::countr_zero(mem.scale));
        const auto mdisp = static_cast<std::uint32_t>(mem.disp);
        if (slot.has_value() && base.has_value() && index.has_value() && mem.scale <= 8 &&
            std::has_single_bit(mem.scale)) {
            return TranslatedInsn{
                .code = kLoadIdxSlot,
                .patches =
                    {
                        {.offset = 5, .size = 4, .value = *base},
                        {.offset = 13, .size = 4, .value = *index},
                        {.offset = 20, .size = 1, .value = shift},
                        {.offset = 27, .size = 4, .value = mdisp},
                        {.offset = 42, .size = 4, .value = mem.size},
                        {.offset = 64, .size = 4, .value = *slot},
                    },
                .call_offset = 48,
                .cpu_offset = 33,
                .helper = JitHelper::kLoad,
            };
        }
    }
    // ALU reg,[rip+disp]: the effective address is rip+length+disp, known at
    // compile time, so the template carries it as one imm64.
    if (insn.ops[0].kind == Operand::Kind::kReg && insn.ops[1].kind == Operand::Kind::kMem &&
        insn.ops[1].rip_relative && insn.ops[0].size == insn.ops[1].size && insn.addr_size == 8 &&
        !insn.has_lock && insn.seg != arch::x86_64::Seg::kFs &&
        insn.seg != arch::x86_64::Seg::kGs &&
        (insn.mnemonic == Mnemonic::kAdd || insn.mnemonic == Mnemonic::kAdc ||
         insn.mnemonic == Mnemonic::kSub || insn.mnemonic == Mnemonic::kSbb ||
         insn.mnemonic == Mnemonic::kAnd || insn.mnemonic == Mnemonic::kOr ||
         insn.mnemonic == Mnemonic::kXor)) {
        const auto slot = operand_slot(insn.ops[0], insn.rex_present);
        const auto mn = static_cast<std::uint64_t>(insn.mnemonic);
        const auto vaddr = rip + insn.length + static_cast<std::uint64_t>(insn.ops[1].disp);
        if (slot.has_value()) {
            return TranslatedInsn{
                .code = kAluMemRipSlot,
                .patches =
                    {
                        {.offset = 2, .size = 4, .value = *slot},
                        {.offset = 8, .size = 8, .value = vaddr},
                        {.offset = 17, .size = 4, .value = mn},
                        {.offset = 23, .size = 4, .value = insn.ops[0].size},
                    },
                .call_offset = 39,
                .cpu_offset = 29,
                .helper = JitHelper::kAluMemRip,
            };
        }
    }
    // movzx reg,[rip+disp]: same constant-address load as the base form.
    if (insn.mnemonic == Mnemonic::kMovzx && insn.ops[0].kind == Operand::Kind::kReg &&
        insn.ops[1].kind == Operand::Kind::kMem && insn.ops[1].rip_relative &&
        (insn.ops[0].size == 4 || insn.ops[0].size == 8) &&
        (insn.ops[1].size == 1 || insn.ops[1].size == 2) && insn.addr_size == 8 && !insn.has_lock &&
        insn.seg != arch::x86_64::Seg::kFs && insn.seg != arch::x86_64::Seg::kGs) {
        const auto slot = operand_slot(insn.ops[0], insn.rex_present);
        const auto vaddr = rip + insn.length + static_cast<std::uint64_t>(insn.ops[1].disp);
        if (slot.has_value()) {
            return TranslatedInsn{
                .code = kLoadRipSlot,
                .patches =
                    {
                        {.offset = 3, .size = 8, .value = vaddr},
                        {.offset = 22, .size = 4, .value = insn.ops[1].size},
                        {.offset = 44, .size = 4, .value = *slot},
                    },
                .call_offset = 28,
                .cpu_offset = 13,
                .helper = JitHelper::kLoadRip,
            };
        }
    }
    // imul reg,[base+disp]: two-operand form, multiplier read from memory.
    if (insn.mnemonic == Mnemonic::kImul && insn.ops[0].kind == Operand::Kind::kReg &&
        insn.ops[1].kind == Operand::Kind::kMem && insn.ops[1].has_base && !insn.ops[1].has_index &&
        !insn.ops[1].rip_relative && insn.ops[0].size == insn.ops[1].size && insn.addr_size == 8 &&
        !insn.has_lock && insn.seg != arch::x86_64::Seg::kFs &&
        insn.seg != arch::x86_64::Seg::kGs && insn.ops[1].disp >= INT32_MIN &&
        insn.ops[1].disp <= INT32_MAX && (insn.ops[0].size == 4 || insn.ops[0].size == 8)) {
        const auto slot = operand_slot(insn.ops[0], insn.rex_present);
        const auto base = reg_slot_offset(insn.ops[1].base);
        const auto idisp = static_cast<std::uint32_t>(insn.ops[1].disp);
        if (slot.has_value() && base.has_value()) {
            return TranslatedInsn{
                .code = kImulMemSlot,
                .patches =
                    {
                        {.offset = 4, .size = 4, .value = *base},
                        {.offset = 11, .size = 4, .value = idisp},
                        {.offset = 16, .size = 4, .value = *slot},
                        {.offset = 21, .size = 4, .value = insn.ops[0].size},
                    },
                .call_offset = 37,
                .cpu_offset = 27,
                .helper = JitHelper::kImulMem,
            };
        }
    }
    // ALU [base+disp],imm: read-modify-write in one helper so the memory
    // operand is touched exactly once per direction.
    if (insn.ops[0].kind == Operand::Kind::kMem && insn.ops[1].kind == Operand::Kind::kImm &&
        insn.ops[0].has_base && !insn.ops[0].has_index && !insn.ops[0].rip_relative &&
        insn.ops[0].size == insn.opsize && insn.addr_size == 8 && !insn.has_lock &&
        insn.seg != arch::x86_64::Seg::kFs && insn.seg != arch::x86_64::Seg::kGs &&
        insn.ops[0].disp >= INT32_MIN && insn.ops[0].disp <= INT32_MAX &&
        (insn.mnemonic == Mnemonic::kAdd || insn.mnemonic == Mnemonic::kSub ||
         insn.mnemonic == Mnemonic::kAnd || insn.mnemonic == Mnemonic::kOr ||
         insn.mnemonic == Mnemonic::kXor || insn.mnemonic == Mnemonic::kCmp)) {
        const auto base = reg_slot_offset(insn.ops[0].base);
        const auto mdisp = static_cast<std::uint32_t>(insn.ops[0].disp);
        const auto immv = static_cast<std::uint64_t>(insn.ops[1].disp);
        const auto mmn = static_cast<unsigned>(insn.mnemonic);
        if (base.has_value()) {
            return TranslatedInsn{
                .code = kAluMemImmSlot,
                .patches =
                    {
                        {.offset = 4, .size = 4, .value = *base},
                        {.offset = 11, .size = 4, .value = mdisp},
                        {.offset = 17, .size = 8, .value = immv},
                        {.offset = 26, .size = 4, .value = mmn},
                        {.offset = 32, .size = 4, .value = insn.ops[0].size},
                    },
                .call_offset = 48,
                .cpu_offset = 38,
                .helper = JitHelper::kAluMemImm,
            };
        }
    }
    // cmov reg,[base+disp]: load the source, merge only when cc holds. The
    // helper reads memory after the condition test, so a false condition
    // costs nothing and a faulted read writes nothing.
    if (insn.mnemonic == Mnemonic::kCmovcc && insn.ops[0].kind == Operand::Kind::kReg &&
        insn.ops[1].kind == Operand::Kind::kMem && insn.ops[1].has_base && !insn.ops[1].has_index &&
        !insn.ops[1].rip_relative && insn.ops[0].size == insn.ops[1].size && insn.addr_size == 8 &&
        !insn.has_lock && insn.seg != arch::x86_64::Seg::kFs &&
        insn.seg != arch::x86_64::Seg::kGs && insn.ops[1].disp >= INT32_MIN &&
        insn.ops[1].disp <= INT32_MAX) {
        const auto slot = operand_slot(insn.ops[0], insn.rex_present);
        const auto base = reg_slot_offset(insn.ops[1].base);
        const auto cdisp = static_cast<std::uint32_t>(insn.ops[1].disp);
        if (slot.has_value() && base.has_value()) {
            return TranslatedInsn{
                .code = kCmovMemSlot,
                .patches =
                    {
                        {.offset = 4, .size = 4, .value = *base},
                        {.offset = 11, .size = 4, .value = cdisp},
                        {.offset = 16, .size = 4, .value = static_cast<unsigned>(insn.cc)},
                        {.offset = 21, .size = 4, .value = *slot},
                        {.offset = 27, .size = 4, .value = insn.ops[0].size},
                    },
                .call_offset = 43,
                .cpu_offset = 33,
                .helper = JitHelper::kCmovMem,
            };
        }
    }
    // movabs r64,imm64: host-only movabs through rax; full 64-bit
    // immediates (kernel high-half addresses) need no helper at all.
    if (insn.mnemonic == Mnemonic::kMovabs && insn.ops[0].kind == Operand::Kind::kReg &&
        insn.ops[1].kind == Operand::Kind::kImm && insn.opsize == 8) {
        const auto slot = reg_slot_offset(insn.ops[0].reg);
        const auto imm64 = static_cast<std::uint64_t>(insn.ops[1].disp);
        if (slot.has_value()) {
            return TranslatedInsn{
                .code = kMovAbsSlot,
                .patches =
                    {
                        {.offset = 2, .size = 8, .value = imm64},
                        {.offset = 13, .size = 4, .value = *slot},
                    },
                .call_offset = std::nullopt,
                .cpu_offset = std::nullopt,
                .helper = JitHelper::kNone,
            };
        }
    }
    // movzx reg,reg and mov r16,r16 share one helper; zx picks the write.
    if ((insn.mnemonic == Mnemonic::kMovzx || insn.mnemonic == Mnemonic::kMov) &&
        insn.ops[0].kind == Operand::Kind::kReg && insn.ops[1].kind == Operand::Kind::kReg &&
        insn.addr_size == 8 && !insn.has_lock) {
        const bool movzx = insn.mnemonic == Mnemonic::kMovzx;
        if (movzx || insn.ops[0].size == 2) {
            const auto dst = reg_slot_offset(insn.ops[0].reg);
            const auto src = reg_slot_offset(insn.ops[1].reg);
            if (dst.has_value() && src.has_value()) {
                return TranslatedInsn{
                    .code = kMovSlotW,
                    .patches =
                        {
                            {.offset = 4, .size = 4, .value = *src},
                            {.offset = 9, .size = 4, .value = movzx ? insn.ops[1].size : 2U},
                            {.offset = 14, .size = 4, .value = *dst},
                            {.offset = 20, .size = 4, .value = movzx ? 1U : 0U},
                        },
                    .call_offset = 36,
                    .cpu_offset = 26,
                    .helper = JitHelper::kMovSlotW,
                };
            }
        }
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
    // mov [base+disp],imm: the constant rides in the patched imm64 hole.
    if (insn.mnemonic == Mnemonic::kMov && insn.ops[0].kind == Operand::Kind::kMem &&
        insn.ops[0].has_base && !insn.ops[0].has_index && !insn.ops[0].rip_relative &&
        memory_width(insn.ops[0].size) && insn.ops[1].kind == Operand::Kind::kImm &&
        insn.ops[1].disp >= INT32_MIN && insn.ops[1].disp <= INT32_MAX) {
        const auto base = reg_slot_offset(insn.ops[0].base);
        if (!base.has_value()) {
            return std::nullopt;
        }
        std::vector<JitPage::Patch> patches = {
            JitPage::Patch{
                .offset = 3,
                .size = 8,
                .value = static_cast<uint64_t>(insn.ops[1].disp),
            },
            JitPage::Patch{.offset = 15, .size = 4, .value = *base},
            JitPage::Patch{
                .offset = 22,
                .size = 4,
                .value = static_cast<std::uint32_t>(insn.ops[0].disp),
            },
            JitPage::Patch{.offset = 37, .size = 4, .value = insn.ops[0].size},
        };
        return TranslatedInsn{
            .code = kStoreImmSlot,
            .patches = std::move(patches),
            .call_offset = 43,
            .cpu_offset = 28,
            .helper = JitHelper::kStore,
        };
    }
    // mov reg,[base+index*scale+disp]: compute vaddr before calling the helper.
    if (insn.mnemonic == Mnemonic::kMov && insn.ops[0].kind == Operand::Kind::kReg &&
        insn.ops[1].kind == Operand::Kind::kMem && insn.ops[1].has_base &&
        !insn.ops[1].rip_relative && memory_width(insn.ops[0].size) &&
        insn.ops[0].size == insn.ops[1].size && insn.addr_size == 8 && !insn.has_lock &&
        insn.seg != arch::x86_64::Seg::kFs && insn.seg != arch::x86_64::Seg::kGs &&
        insn.ops[1].disp >= INT32_MIN && insn.ops[1].disp <= INT32_MAX) {
        const auto slot = operand_slot(insn.ops[0], insn.rex_present);
        if (!slot.has_value()) {
            return std::nullopt;
        }
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
    // mov reg,[rip+disp]: rip_after + disp is a compile-time constant.
    if (insn.mnemonic == Mnemonic::kMov && insn.ops[0].kind == Operand::Kind::kReg &&
        insn.ops[1].kind == Operand::Kind::kMem && insn.ops[1].rip_relative &&
        insn.ops[0].size == 8) {
        const auto slot = operand_slot(insn.ops[0], insn.rex_present);
        if (!slot.has_value()) {
            return std::nullopt;
        }
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
    return std::nullopt;
}

} // namespace micro_forge::base::detail
