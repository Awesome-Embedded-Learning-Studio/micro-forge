/**
 * @file jit_translate_mov.cpp
 * @brief Internal split of jit_translate.cpp: mov-family lowering only.
 */

#include "jit_translate_mov.hpp"

#include "micro_forge/base/jit_templates_generated.hpp"

#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace micro_forge::base::detail {

namespace jg = micro_forge::base::jit_gen;

namespace {

using arch::x86_64::Mnemonic;
using arch::x86_64::Operand;

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
                .code = jg::kLoadSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kLoadSlotHoles, jg::HoleKind::kBase32, *base),
                        jg::hole_patch(jg::kLoadSlotHoles, jg::HoleKind::kDisp32, disp),
                        jg::hole_patch(jg::kLoadSlotHoles, jg::HoleKind::kWidth32,
                                       insn.ops[1].size),
                        jg::hole_patch(jg::kLoadSlotHoles, jg::HoleKind::kDst32, *slot),
                    },
                .call_offset =
                    jg::hole_patch(jg::kLoadSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset = jg::hole_patch(jg::kLoadSlotHoles, jg::HoleKind::kCpu64, 0).offset,
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
                .code = jg::kAluMemSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kAluMemSlotHoles, jg::HoleKind::kSlot32, *slot),
                        jg::hole_patch(jg::kAluMemSlotHoles, jg::HoleKind::kBase32, *base),
                        jg::hole_patch(jg::kAluMemSlotHoles, jg::HoleKind::kDisp32, adisp),
                        jg::hole_patch(jg::kAluMemSlotHoles, jg::HoleKind::kMnem32, amn),
                        jg::hole_patch(jg::kAluMemSlotHoles, jg::HoleKind::kWidth32,
                                       insn.ops[0].size),
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
                .code = jg::kLoadIdxSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kLoadIdxSlotHoles, jg::HoleKind::kBase32, *base),
                        jg::hole_patch(jg::kLoadIdxSlotHoles, jg::HoleKind::kIdx32, *index),
                        jg::hole_patch(jg::kLoadIdxSlotHoles, jg::HoleKind::kShift8, shift),
                        jg::hole_patch(jg::kLoadIdxSlotHoles, jg::HoleKind::kDisp32, mdisp),
                        jg::hole_patch(jg::kLoadIdxSlotHoles, jg::HoleKind::kWidth32, mem.size),
                        jg::hole_patch(jg::kLoadIdxSlotHoles, jg::HoleKind::kDst32, *slot),
                    },
                .call_offset =
                    jg::hole_patch(jg::kLoadIdxSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset = jg::hole_patch(jg::kLoadIdxSlotHoles, jg::HoleKind::kCpu64, 0).offset,
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
                .code = jg::kAluMemRipSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kAluMemRipSlotHoles, jg::HoleKind::kSlot32, *slot),
                        jg::hole_patch(jg::kAluMemRipSlotHoles, jg::HoleKind::kVaddr64, vaddr),
                        jg::hole_patch(jg::kAluMemRipSlotHoles, jg::HoleKind::kMnem32, mn),
                        jg::hole_patch(jg::kAluMemRipSlotHoles, jg::HoleKind::kWidth32,
                                       insn.ops[0].size),
                    },
                .call_offset =
                    jg::hole_patch(jg::kAluMemRipSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset =
                    jg::hole_patch(jg::kAluMemRipSlotHoles, jg::HoleKind::kCpu64, 0).offset,
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
                .code = jg::kLoadRipSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kLoadRipSlotHoles, jg::HoleKind::kVaddr64, vaddr),
                        jg::hole_patch(jg::kLoadRipSlotHoles, jg::HoleKind::kWidth32,
                                       insn.ops[1].size),
                        jg::hole_patch(jg::kLoadRipSlotHoles, jg::HoleKind::kDst32, *slot),
                    },
                .call_offset =
                    jg::hole_patch(jg::kLoadRipSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset = jg::hole_patch(jg::kLoadRipSlotHoles, jg::HoleKind::kCpu64, 0).offset,
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
                .code = jg::kImulMemSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kImulMemSlotHoles, jg::HoleKind::kBase32, *base),
                        jg::hole_patch(jg::kImulMemSlotHoles, jg::HoleKind::kDisp32, idisp),
                        jg::hole_patch(jg::kImulMemSlotHoles, jg::HoleKind::kDst32, *slot),
                        jg::hole_patch(jg::kImulMemSlotHoles, jg::HoleKind::kWidth32,
                                       insn.ops[0].size),
                    },
                .call_offset =
                    jg::hole_patch(jg::kImulMemSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset = jg::hole_patch(jg::kImulMemSlotHoles, jg::HoleKind::kCpu64, 0).offset,
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
                .code = jg::kAluMemImmSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kAluMemImmSlotHoles, jg::HoleKind::kBase32, *base),
                        jg::hole_patch(jg::kAluMemImmSlotHoles, jg::HoleKind::kDisp32, mdisp),
                        jg::hole_patch(jg::kAluMemImmSlotHoles, jg::HoleKind::kImm64, immv),
                        jg::hole_patch(jg::kAluMemImmSlotHoles, jg::HoleKind::kMnem32, mmn),
                        jg::hole_patch(jg::kAluMemImmSlotHoles, jg::HoleKind::kWidth32,
                                       insn.ops[0].size),
                    },
                .call_offset =
                    jg::hole_patch(jg::kAluMemImmSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset =
                    jg::hole_patch(jg::kAluMemImmSlotHoles, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kAluMemImm,
            };
        }
    }
    // movsx/movsxd reg,reg: sign-extend the source into a wider register.
    // The kMovSlotW byte layout already carries value/width/dst/width.
    if (insn.mnemonic == Mnemonic::kMovsx && insn.ops[0].kind == Operand::Kind::kReg &&
        insn.ops[1].kind == Operand::Kind::kReg && insn.addr_size == 8 && !insn.has_lock &&
        (insn.ops[0].size == 4 || insn.ops[0].size == 8) &&
        (insn.ops[1].size == 1 || insn.ops[1].size == 2 || insn.ops[1].size == 4) &&
        insn.ops[1].size < insn.ops[0].size) {
        const auto dst = operand_slot(insn.ops[0], insn.rex_present);
        const auto src = reg_slot_offset(insn.ops[1].reg);
        if (dst.has_value() && src.has_value()) {
            return TranslatedInsn{
                .code = jg::kMovSlotW,
                .patches =
                    {
                        jg::hole_patch(jg::kMovSlotWHoles, jg::HoleKind::kSrc32, *src),
                        jg::hole_patch(jg::kMovSlotWHoles, jg::HoleKind::kSrcw32, insn.ops[1].size),
                        jg::hole_patch(jg::kMovSlotWHoles, jg::HoleKind::kDst32, *dst),
                        jg::hole_patch(jg::kMovSlotWHoles, jg::HoleKind::kZx32, insn.ops[0].size),
                    },
                .call_offset =
                    jg::hole_patch(jg::kMovSlotWHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset = jg::hole_patch(jg::kMovSlotWHoles, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kMovsx,
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
                .code = jg::kCmovMemSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kCmovMemSlotHoles, jg::HoleKind::kBase32, *base),
                        jg::hole_patch(jg::kCmovMemSlotHoles, jg::HoleKind::kDisp32, cdisp),
                        jg::hole_patch(jg::kCmovMemSlotHoles, jg::HoleKind::kCc32,
                                       static_cast<unsigned>(insn.cc)),
                        jg::hole_patch(jg::kCmovMemSlotHoles, jg::HoleKind::kSlot32, *slot),
                        jg::hole_patch(jg::kCmovMemSlotHoles, jg::HoleKind::kWidth32,
                                       insn.ops[0].size),
                    },
                .call_offset =
                    jg::hole_patch(jg::kCmovMemSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset = jg::hole_patch(jg::kCmovMemSlotHoles, jg::HoleKind::kCpu64, 0).offset,
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
                .code = jg::kMovAbsSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kMovAbsSlotHoles, jg::HoleKind::kImm64, imm64),
                        jg::hole_patch(jg::kMovAbsSlotHoles, jg::HoleKind::kDst32, *slot),
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
                    .code = jg::kMovSlotW,
                    .patches =
                        {
                            jg::hole_patch(jg::kMovSlotWHoles, jg::HoleKind::kSrc32, *src),
                            jg::hole_patch(jg::kMovSlotWHoles, jg::HoleKind::kSrcw32,
                                           movzx ? insn.ops[1].size : 2U),
                            jg::hole_patch(jg::kMovSlotWHoles, jg::HoleKind::kDst32, *dst),
                            jg::hole_patch(jg::kMovSlotWHoles, jg::HoleKind::kZx32,
                                           movzx ? 1U : 0U),
                        },
                    .call_offset =
                        jg::hole_patch(jg::kMovSlotWHoles, jg::HoleKind::kHelper64, 0).offset,
                    .cpu_offset =
                        jg::hole_patch(jg::kMovSlotWHoles, jg::HoleKind::kCpu64, 0).offset,
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
                .code = jg::kStoreIdxSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kStoreIdxSlotHoles, jg::HoleKind::kSrc32, *source),
                        jg::hole_patch(jg::kStoreIdxSlotHoles, jg::HoleKind::kBase32, *base),
                        jg::hole_patch(jg::kStoreIdxSlotHoles, jg::HoleKind::kIdx32, *index),
                        jg::hole_patch(jg::kStoreIdxSlotHoles, jg::HoleKind::kShift8,
                                       static_cast<unsigned>(std::countr_zero(mem.scale))),
                        jg::hole_patch(jg::kStoreIdxSlotHoles, jg::HoleKind::kDisp32,
                                       static_cast<std::uint32_t>(mem.disp)),
                        jg::hole_patch(jg::kStoreIdxSlotHoles, jg::HoleKind::kWidth32, mem.size),
                    },
                .call_offset =
                    jg::hole_patch(jg::kStoreIdxSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset =
                    jg::hole_patch(jg::kStoreIdxSlotHoles, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kStore,
            };
        }
        std::vector<JitPage::Patch> patches = {
            jg::hole_patch(jg::kStoreSlotHoles, jg::HoleKind::kSrc32, *source),
            jg::hole_patch(jg::kStoreSlotHoles, jg::HoleKind::kBase32, *base),
            jg::hole_patch(jg::kStoreSlotHoles, jg::HoleKind::kDisp32,
                           static_cast<std::uint32_t>(insn.ops[0].disp)),
            jg::hole_patch(jg::kStoreSlotHoles, jg::HoleKind::kWidth32, mem.size),
        };
        return TranslatedInsn{
            .code = jg::kStoreSlot,
            .patches = std::move(patches),
            .call_offset = jg::hole_patch(jg::kStoreSlotHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kStoreSlotHoles, jg::HoleKind::kCpu64, 0).offset,
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
            jg::hole_patch(jg::kStoreImmSlotHoles, jg::HoleKind::kImm64,
                           static_cast<uint64_t>(insn.ops[1].disp)),
            jg::hole_patch(jg::kStoreImmSlotHoles, jg::HoleKind::kBase32, *base),
            jg::hole_patch(jg::kStoreImmSlotHoles, jg::HoleKind::kDisp32,
                           static_cast<std::uint32_t>(insn.ops[0].disp)),
            jg::hole_patch(jg::kStoreImmSlotHoles, jg::HoleKind::kWidth32, insn.ops[0].size),
        };
        return TranslatedInsn{
            .code = jg::kStoreImmSlot,
            .patches = std::move(patches),
            .call_offset =
                jg::hole_patch(jg::kStoreImmSlotHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kStoreImmSlotHoles, jg::HoleKind::kCpu64, 0).offset,
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
            // Byte and word writes use shorter variants; each carries its
            // own hole table, hence the span.
            const auto iholes = load_idx_holes(mem.size);
            return TranslatedInsn{
                .code = load_idx_bytes(mem.size),
                .patches =
                    {
                        jg::hole_patch(iholes, jg::HoleKind::kBase32, *base),
                        jg::hole_patch(iholes, jg::HoleKind::kIdx32, *index),
                        jg::hole_patch(iholes, jg::HoleKind::kShift8,
                                       static_cast<unsigned>(std::countr_zero(mem.scale))),
                        jg::hole_patch(iholes, jg::HoleKind::kDisp32,
                                       static_cast<std::uint32_t>(mem.disp)),
                        jg::hole_patch(iholes, jg::HoleKind::kWidth32, mem.size),
                        jg::hole_patch(iholes, jg::HoleKind::kDst32, *slot),
                    },
                .call_offset = jg::hole_patch(iholes, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset = jg::hole_patch(iholes, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kLoad,
            };
        }
        // Byte and word writes use shorter variants with their own hole
        // tables, hence the span.
        const auto lholes = load_holes(mem.size);
        return TranslatedInsn{
            .code = load_bytes(mem.size),
            .patches =
                {
                    jg::hole_patch(lholes, jg::HoleKind::kBase32, *base),
                    jg::hole_patch(lholes, jg::HoleKind::kDisp32,
                                   static_cast<std::uint32_t>(insn.ops[1].disp)),
                    jg::hole_patch(lholes, jg::HoleKind::kWidth32, mem.size),
                    jg::hole_patch(lholes, jg::HoleKind::kDst32, *slot),
                },
            .call_offset = jg::hole_patch(lholes, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(lholes, jg::HoleKind::kCpu64, 0).offset,
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
            jg::hole_patch(jg::kLoadRipSlotHoles, jg::HoleKind::kVaddr64, vaddr),
            jg::hole_patch(jg::kLoadRipSlotHoles, jg::HoleKind::kWidth32, 8),
            jg::hole_patch(jg::kLoadRipSlotHoles, jg::HoleKind::kDst32, *slot),
        };
        return TranslatedInsn{
            .code = jg::kLoadRipSlot,
            .patches = std::move(patches),
            .call_offset = jg::hole_patch(jg::kLoadRipSlotHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kLoadRipSlotHoles, jg::HoleKind::kCpu64, 0).offset,
            .helper = JitHelper::kLoad,
        };
    }
    return std::nullopt;
}

} // namespace micro_forge::base::detail
