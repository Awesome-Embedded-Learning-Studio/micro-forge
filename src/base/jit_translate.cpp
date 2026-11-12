/**
 * @file jit_translate.cpp
 * @brief Copy-and-patch lowering for the first instruction slice.
 * @version 0.1.0
 */

#include "micro_forge/base/jit_translate.hpp"

#include "jit_translate_alu.hpp"

#include "jit_translate_mov.hpp"
#include "jit_translate_movx.hpp"

#include "micro_forge/base/jit_templates_generated.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>

namespace micro_forge::base {

namespace {

using arch::x86_64::Mnemonic;
using arch::x86_64::Operand;
using arch::x86_64::Reg;

namespace jg = micro_forge::base::jit_gen;

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
    // Guest nops (single, xchg ax,ax, 0F 1F padding) lower to one host nop:
    // no flags, no memory, nothing to model.
    if (insn.mnemonic == arch::x86_64::Mnemonic::kNop && insn.addr_size == 8 && !insn.has_lock) {
        return TranslatedInsn{
            .code = jg::kNopJit,
            .patches = {},
            .call_offset = std::nullopt,
            .cpu_offset = std::nullopt,
            .helper = JitHelper::kNone,
        };
    }
    // ret C3: the helper pops [rsp] into the block exit target; the host
    // ret terminates the block. imm16 cleanup (C2) stays interpreter-only.
    if (insn.mnemonic == Mnemonic::kRet && insn.addr_size == 8 && !insn.has_lock &&
        insn.ops[0].kind != Operand::Kind::kImm) {
        const auto rsp_slot = reg_slot_offset(Reg::kRsp);
        if (rsp_slot.has_value()) {
            return TranslatedInsn{
                .code = jg::kRetHelper,
                .patches =
                    {
                        jg::hole_patch(jg::kRetHelperHoles, jg::HoleKind::kRsp32, *rsp_slot),
                        jg::hole_patch(jg::kRetHelperHoles, jg::HoleKind::kMyRip64, rip),
                    },
                .call_offset =
                    jg::hole_patch(jg::kRetHelperHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset = jg::hole_patch(jg::kRetHelperHoles, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kRet,
            };
        }
        return std::nullopt;
    }
    // call reg64 / rel32: helper pushes the return address and records the
    // exit target; the host ret terminates the block. mem form stays out.
    if (insn.mnemonic == Mnemonic::kCall && insn.addr_size == 8 && !insn.has_lock) {
        if (insn.ops[0].kind == Operand::Kind::kImm) {
            const uint64_t ret_addr = rip + insn.length;
            const uint64_t target = ret_addr + static_cast<uint64_t>(insn.ops[0].disp);
            return TranslatedInsn{
                .code = jg::kCallImmHelper,
                .patches =
                    {
                        jg::hole_patch(jg::kCallImmHelperHoles, jg::HoleKind::kTarget64, target),
                        jg::hole_patch(jg::kCallImmHelperHoles, jg::HoleKind::kMyRip64, rip),
                        jg::hole_patch(jg::kCallImmHelperHoles, jg::HoleKind::kRetAddr64, ret_addr),
                    },
                .call_offset =
                    jg::hole_patch(jg::kCallImmHelperHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset =
                    jg::hole_patch(jg::kCallImmHelperHoles, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kCall,
            };
        }
        if (insn.ops[0].kind == Operand::Kind::kReg && insn.ops[0].size == 8) {
            const auto reg = reg_slot_offset(insn.ops[0].reg);
            const uint64_t ret_addr = rip + insn.length;
            if (reg.has_value()) {
                return TranslatedInsn{
                    .code = jg::kCallRegHelper,
                    .patches =
                        {
                            jg::hole_patch(jg::kCallRegHelperHoles, jg::HoleKind::kReg32, *reg),
                            jg::hole_patch(jg::kCallRegHelperHoles, jg::HoleKind::kMyRip64, rip),
                            jg::hole_patch(jg::kCallRegHelperHoles, jg::HoleKind::kRetAddr64,
                                           ret_addr),
                        },
                    .call_offset =
                        jg::hole_patch(jg::kCallRegHelperHoles, jg::HoleKind::kHelper64, 0).offset,
                    .cpu_offset =
                        jg::hole_patch(jg::kCallRegHelperHoles, jg::HoleKind::kCpu64, 0).offset,
                    .helper = JitHelper::kCall,
                };
            }
        }
        return std::nullopt;
    }
    // shift reg,imm8 (SHL/SHR/SAR/ROL/ROR/RCL/RCR): the helper runs the
    // interpreter's alu_shift so every flag matches; count 0 keeps flags.
    if (insn.mnemonic == Mnemonic::kShl || insn.mnemonic == Mnemonic::kShr ||
        insn.mnemonic == Mnemonic::kSar || insn.mnemonic == Mnemonic::kRol ||
        insn.mnemonic == Mnemonic::kRor || insn.mnemonic == Mnemonic::kRcl ||
        insn.mnemonic == Mnemonic::kRcr) {
        const auto slot = reg_slot_offset(insn.ops[0].reg);
        if (insn.ops[0].kind == Operand::Kind::kReg && insn.ops[1].kind == Operand::Kind::kImm &&
            slot.has_value() && insn.addr_size == 8 && !insn.has_lock && insn.ops[0].size != 0 &&
            *slot % sizeof(std::uint64_t) == 0) {
            const auto count = static_cast<uint64_t>(insn.ops[1].disp);
            const auto mn = static_cast<unsigned>(insn.mnemonic);
            return TranslatedInsn{
                .code = jg::kShiftImmSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kShiftImmSlotHoles, jg::HoleKind::kSlot32, *slot),
                        jg::hole_patch(jg::kShiftImmSlotHoles, jg::HoleKind::kCount32, count),
                        jg::hole_patch(jg::kShiftImmSlotHoles, jg::HoleKind::kMnem32, mn),
                        jg::hole_patch(jg::kShiftImmSlotHoles, jg::HoleKind::kWidth32,
                                       insn.ops[0].size),
                    },
                .call_offset =
                    jg::hole_patch(jg::kShiftImmSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset =
                    jg::hole_patch(jg::kShiftImmSlotHoles, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kShift,
            };
        }
    }
    // jmp imm: record the patched target; the host ret ends the block.
    if (insn.mnemonic == Mnemonic::kJmp && insn.addr_size == 8 && !insn.has_lock &&
        insn.ops[0].kind == Operand::Kind::kImm) {
        const uint64_t target = rip + insn.length + static_cast<uint64_t>(insn.ops[0].disp);
        return TranslatedInsn{
            .code = jg::kJmpImmHelper,
            .patches =
                {
                    jg::hole_patch(jg::kJmpImmHelperHoles, jg::HoleKind::kTarget64, target),
                },
            .call_offset =
                jg::hole_patch(jg::kJmpImmHelperHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kJmpImmHelperHoles, jg::HoleKind::kCpu64, 0).offset,
            .helper = JitHelper::kJmp,
        };
    }
    // jcc: taken/fall patched; the helper evaluates the guest condition.
    if (insn.mnemonic == Mnemonic::kJcc && insn.addr_size == 8 && !insn.has_lock &&
        insn.ops[0].kind == Operand::Kind::kImm) {
        const uint64_t fall = rip + insn.length;
        const uint64_t taken = fall + static_cast<uint64_t>(insn.ops[0].disp);
        const auto cc = static_cast<uint64_t>(insn.cc);
        return TranslatedInsn{
            .code = jg::kJccHelper,
            .patches =
                {
                    jg::hole_patch(jg::kJccHelperHoles, jg::HoleKind::kTaken64, taken),
                    jg::hole_patch(jg::kJccHelperHoles, jg::HoleKind::kFall64, fall),
                    jg::hole_patch(jg::kJccHelperHoles, jg::HoleKind::kCc32, cc),
                },
            .call_offset = jg::hole_patch(jg::kJccHelperHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kJccHelperHoles, jg::HoleKind::kCpu64, 0).offset,
            .helper = JitHelper::kJcc,
        };
    }
    // leave: rsp=rbp then pop rbp; helper-faulted reads latch like stores.
    if (insn.mnemonic == Mnemonic::kLeave && insn.addr_size == 8 && !insn.has_lock &&
        insn.ops[0].kind != Operand::Kind::kImm) {
        return TranslatedInsn{
            .code = jg::kLeaveHelper,
            .patches = {},
            .call_offset = jg::hole_patch(jg::kLeaveHelperHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kLeaveHelperHoles, jg::HoleKind::kCpu64, 0).offset,
            .helper = JitHelper::kLeave,
        };
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
            .code = jg::kAddMemSlot,
            .patches =
                {
                    jg::hole_patch(jg::kAddMemSlotHoles, jg::HoleKind::kSrc32, *source),
                    jg::hole_patch(jg::kAddMemSlotHoles, jg::HoleKind::kBase32, *base),
                    jg::hole_patch(jg::kAddMemSlotHoles, jg::HoleKind::kDisp32,
                                   static_cast<std::uint32_t>(insn.ops[0].disp)),
                },
            .call_offset = jg::hole_patch(jg::kAddMemSlotHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kAddMemSlotHoles, jg::HoleKind::kCpu64, 0).offset,
            .helper = JitHelper::kAddMem,
        };
    }
    // mov family lives in jit_translate_mov.cpp: store reg/imm, load reg.
    if (auto moved = detail::translate_move(insn, rip)) {
        return moved;
    }
    // push imm: the value is already sign-extended into ops[0].disp.
    if (insn.addr_size == 8 && !insn.has_lock && insn.mnemonic == Mnemonic::kPush &&
        insn.ops[0].kind == Operand::Kind::kImm) {
        const auto imm_patch = jg::hole_patch(jg::kPushImmHelperHoles, jg::HoleKind::kImm64,
                                              static_cast<std::uint64_t>(insn.ops[0].disp));
        return TranslatedInsn{
            .code = jg::kPushImmHelper,
            .patches = {imm_patch},
            .call_offset =
                jg::hole_patch(jg::kPushImmHelperHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kPushImmHelperHoles, jg::HoleKind::kCpu64, 0).offset,
            .helper = JitHelper::kPushImm,
        };
    }
    if (auto movx = detail::translate_movx(insn)) {
        return movx;
    }
    // shift-by-cl and pxor need their own templates; pxor's destination is
    // an xmm register, so this must run before the GPR-only guard below.
    if (auto extra = detail::translate_alu_extra(insn, rip)) {
        return extra;
    }
    // Stores have a memory destination; remaining forms share the reg lookup.
    // kImplicit covers the accumulator short forms (48 3D cmp rax,imm32 and
    // friends), where the register is encoded in the opcode rather than modrm.
    if ((insn.ops[0].kind != Operand::Kind::kReg && insn.ops[0].kind != Operand::Kind::kImplicit) ||
        !detail::memory_width(insn.ops[0].size)) {
        return std::nullopt;
    }
    const auto slot = detail::operand_slot(insn.ops[0], insn.rex_present);
    if (!slot.has_value()) {
        return std::nullopt;
    }
    // add/adc/sub/sbb/and/or/xor/cmp reg,reg: one helper runs the same
    // alu_add/alu_sub/alu_logic the interpreter uses, so flags cannot drift.
    if (insn.ops[1].kind == Operand::Kind::kReg && insn.ops[0].size == insn.ops[1].size &&
        insn.addr_size == 8 && !insn.has_lock) {
        // add/adc/sbb are the only family members without a dedicated
        // translate_alu path; and/or/xor/cmp/sub keep their shorter form.
        const bool reg_alu = insn.mnemonic == Mnemonic::kAdd || insn.mnemonic == Mnemonic::kAdc ||
                             insn.mnemonic == Mnemonic::kSbb;
        const auto src_slot = reg_slot_offset(insn.ops[1].reg);
        if (reg_alu && src_slot.has_value()) {
            return TranslatedInsn{
                .code = jg::kAluRegSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kAluRegSlotHoles, jg::HoleKind::kSlot32, *slot),
                        jg::hole_patch(jg::kAluRegSlotHoles, jg::HoleKind::kSrc32, *src_slot),
                        jg::hole_patch(jg::kAluRegSlotHoles, jg::HoleKind::kMnem32,
                                       static_cast<unsigned>(insn.mnemonic)),
                        jg::hole_patch(jg::kAluRegSlotHoles, jg::HoleKind::kWidth32,
                                       insn.ops[0].size),
                    },
                .call_offset =
                    jg::hole_patch(jg::kAluRegSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset = jg::hole_patch(jg::kAluRegSlotHoles, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kAluReg,
            };
        }
    }
    const auto helper = detail::alu_helper(insn.mnemonic);
    if (helper != JitHelper::kNone) {
        if (auto lowered = detail::translate_alu(insn, *slot, helper)) {
            return lowered;
        }
    }
    // cmovcc reg,reg: the helper reads the live Cpu flags and keeps the old
    // destination when the condition fails.
    if (insn.mnemonic == Mnemonic::kCmovcc && *slot % sizeof(uint64_t) == 0 &&
        insn.ops[1].kind == Operand::Kind::kReg && insn.ops[1].size == insn.ops[0].size &&
        !insn.has_lock) {
        const auto src_slot = reg_slot_offset(insn.ops[1].reg);
        if (src_slot.has_value()) {
            return TranslatedInsn{
                .code = jg::kCmovRegHelper,
                .patches =
                    {
                        jg::hole_patch(jg::kCmovRegHelperHoles, jg::HoleKind::kDst32, *slot),
                        jg::hole_patch(jg::kCmovRegHelperHoles, jg::HoleKind::kSrc32, *src_slot),
                        jg::hole_patch(jg::kCmovRegHelperHoles, jg::HoleKind::kCc32,
                                       static_cast<std::uint32_t>(insn.cc)),
                        jg::hole_patch(jg::kCmovRegHelperHoles, jg::HoleKind::kWidth32,
                                       insn.ops[0].size),
                    },
                .call_offset =
                    jg::hole_patch(jg::kCmovRegHelperHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset =
                    jg::hole_patch(jg::kCmovRegHelperHoles, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kCmov,
            };
        }
        return std::nullopt;
    }
    // setcc reg8: single-byte conditional write, flags come from the helper.
    if (insn.mnemonic == Mnemonic::kSetcc && *slot % sizeof(uint64_t) == 0 &&
        insn.ops[0].kind == Operand::Kind::kReg && insn.ops[0].size == 1 && !insn.has_lock) {
        return TranslatedInsn{
            .code = jg::kSetccRegHelper,
            .patches =
                {
                    jg::hole_patch(jg::kSetccRegHelperHoles, jg::HoleKind::kDst32, *slot),
                    jg::hole_patch(jg::kSetccRegHelperHoles, jg::HoleKind::kCc32,
                                   static_cast<std::uint32_t>(insn.cc)),
                },
            .call_offset =
                jg::hole_patch(jg::kSetccRegHelperHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kSetccRegHelperHoles, jg::HoleKind::kCpu64, 0).offset,
            .helper = JitHelper::kSetcc,
        };
    }
    // push/pop reg64 and push imm: the helper walks rsp via push_value/pop_value.
    if (insn.addr_size == 8 && !insn.has_lock) {
        if (insn.mnemonic == Mnemonic::kPush && insn.ops[0].kind == Operand::Kind::kReg &&
            insn.ops[0].size == 8 && *slot % sizeof(uint64_t) == 0) {
            return TranslatedInsn{
                .code = jg::kPushRegHelper,
                .patches = {jg::hole_patch(jg::kPushRegHelperHoles, jg::HoleKind::kSlot32, *slot)},
                .call_offset =
                    jg::hole_patch(jg::kPushRegHelperHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset =
                    jg::hole_patch(jg::kPushRegHelperHoles, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kPushVal,
            };
        }
        if (insn.mnemonic == Mnemonic::kPop && insn.ops[0].kind == Operand::Kind::kReg &&
            insn.ops[0].size == 8 && *slot % sizeof(uint64_t) == 0) {
            return TranslatedInsn{
                .code = jg::kPopRegHelper,
                .patches = {jg::hole_patch(jg::kPopRegHelperHoles, jg::HoleKind::kSlot32, *slot)},
                .call_offset =
                    jg::hole_patch(jg::kPopRegHelperHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset =
                    jg::hole_patch(jg::kPopRegHelperHoles, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kPopReg,
            };
        }
    }
    // add reg,imm32: helper updates the value and flags together. kImplicit
    // covers the accumulator short form (48 05 imm32 = add rax,imm32).
    if (insn.mnemonic == Mnemonic::kAdd &&
        (insn.ops[0].kind == Operand::Kind::kReg || insn.ops[0].kind == Operand::Kind::kImplicit) &&
        insn.ops[1].kind == Operand::Kind::kImm &&
        (insn.ops[0].size == 4 || insn.ops[0].size == 8) && insn.ops[1].disp >= INT32_MIN &&
        insn.ops[1].disp <=
            (insn.ops[0].size == 4 ? static_cast<int64_t>(UINT32_MAX) : INT32_MAX)) {
        std::vector<JitPage::Patch> patches = {
            jg::hole_patch(jg::kAddImmHelperHoles, jg::HoleKind::kSlot32, *slot),
            jg::hole_patch(jg::kAddImmHelperHoles, jg::HoleKind::kImm32,
                           static_cast<std::uint32_t>(insn.ops[1].disp)),
            jg::hole_patch(jg::kAddImmHelperHoles, jg::HoleKind::kWidth32, insn.ops[0].size),
        };
        return TranslatedInsn{
            .code = jg::kAddImmHelper,
            .patches = std::move(patches),
            .call_offset =
                jg::hole_patch(jg::kAddImmHelperHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kAddImmHelperHoles, jg::HoleKind::kCpu64, 0).offset,
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
            jg::hole_patch(jg::kLeaSlotHoles, jg::HoleKind::kBase32, *base),
            jg::hole_patch(jg::kLeaSlotHoles, jg::HoleKind::kDisp32,
                           static_cast<std::uint32_t>(insn.ops[1].disp)),
            jg::hole_patch(jg::kLeaSlotHoles, jg::HoleKind::kDst32, *slot),
        };
        return TranslatedInsn{
            .code = jg::kLeaSlot,
            .patches = std::move(patches),
            .call_offset = std::nullopt,
            .cpu_offset = std::nullopt,
        };
    }
    // lea dst,[base+index*scale+disp]: host-side address arithmetic only.
    // The 32-bit dst form re-uses the arithmetic, then zero-extends.
    if (insn.mnemonic == Mnemonic::kLea && insn.ops[1].kind == Operand::Kind::kMem &&
        insn.ops[1].has_base && insn.ops[1].has_index && !insn.ops[1].rip_relative &&
        (insn.ops[0].size == 4 || insn.ops[0].size == 8)) {
        const auto base = reg_slot_offset(insn.ops[1].base);
        const auto index = reg_slot_offset(insn.ops[1].index);
        if (!base.has_value() || !index.has_value()) {
            return std::nullopt;
        }
        unsigned scale8 = 0;
        const bool scale_ok = insn.ops[1].scale == 1 || insn.ops[1].scale == 2 ||
                              insn.ops[1].scale == 4 || insn.ops[1].scale == 8;
        if (scale_ok) {
            scale8 = static_cast<unsigned>(std::countr_zero(insn.ops[1].scale));
        } else {
            return std::nullopt;
        }
        const bool wide = insn.ops[0].size == 8;
        const auto idisp = static_cast<std::uint32_t>(insn.ops[1].disp);
        // The 64- and 32-bit lea forms differ in length, hence different hole
        // offsets; each direction gets its own table instead of a ternary.
        if (wide) {
            return TranslatedInsn{
                .code = jg::kLeaIdxSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kLeaIdxSlotHoles, jg::HoleKind::kBase32, *base),
                        jg::hole_patch(jg::kLeaIdxSlotHoles, jg::HoleKind::kIdx32, *index),
                        jg::hole_patch(jg::kLeaIdxSlotHoles, jg::HoleKind::kScale8, scale8),
                        jg::hole_patch(jg::kLeaIdxSlotHoles, jg::HoleKind::kDisp32, idisp),
                        jg::hole_patch(jg::kLeaIdxSlotHoles, jg::HoleKind::kDst32, *slot),
                    },
                .call_offset = std::nullopt,
                .cpu_offset = std::nullopt,
            };
        }
        return TranslatedInsn{
            .code = jg::kLeaIdxSlot32,
            .patches =
                {
                    jg::hole_patch(jg::kLeaIdxSlot32Holes, jg::HoleKind::kBase32, *base),
                    jg::hole_patch(jg::kLeaIdxSlot32Holes, jg::HoleKind::kIdx32, *index),
                    jg::hole_patch(jg::kLeaIdxSlot32Holes, jg::HoleKind::kScale8, scale8),
                    jg::hole_patch(jg::kLeaIdxSlot32Holes, jg::HoleKind::kDisp32, idisp),
                    jg::hole_patch(jg::kLeaIdxSlot32Holes, jg::HoleKind::kDst32, *slot),
                },
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
            jg::hole_patch(jg::kMovSlotSlotHoles, jg::HoleKind::kSrc32, *source),
            jg::hole_patch(jg::kMovSlotSlotHoles, jg::HoleKind::kDst32, *slot),
        };
        return TranslatedInsn{
            .code = jg::kMovSlotSlot,
            .patches = std::move(patches),
            .call_offset = std::nullopt,
            .cpu_offset = std::nullopt,
        };
    }
    // 32-bit register move: the host 32-bit load zero-extends the result.
    if (insn.mnemonic == Mnemonic::kMov && insn.ops[1].kind == Operand::Kind::kReg &&
        insn.ops[0].size == 4) {
        const auto source = reg_slot_offset(insn.ops[1].reg);
        if (!source.has_value()) {
            return std::nullopt;
        }
        std::vector<JitPage::Patch> patches = {
            jg::hole_patch(jg::kMovSlotSlot32Holes, jg::HoleKind::kSrc32, *source),
            jg::hole_patch(jg::kMovSlotSlot32Holes, jg::HoleKind::kDst32, *slot),
        };
        return TranslatedInsn{
            .code = jg::kMovSlotSlot32,
            .patches = std::move(patches),
            .call_offset = std::nullopt,
            .cpu_offset = std::nullopt,
        };
    }
    // Only guest imm32 constants are lowered; kernel high-half addresses
    // stay with the interpreter rather than being truncated.
    if ((insn.ops[0].size != 4 && insn.ops[0].size != 8) ||
        insn.ops[1].kind != Operand::Kind::kImm || insn.ops[1].disp < INT32_MIN ||
        insn.ops[1].disp > (insn.ops[0].size == 4 ? static_cast<int64_t>(UINT32_MAX) : INT32_MAX)) {
        return std::nullopt;
    }
    const auto imm = insn.ops[0].size == 4 ? static_cast<std::uint32_t>(insn.ops[1].disp)
                                           : static_cast<uint64_t>(insn.ops[1].disp);
    if (insn.mnemonic == Mnemonic::kMov) {
        return TranslatedInsn{
            .code = jg::kMovSlotImm,
            .patches =
                {
                    jg::hole_patch(jg::kMovSlotImmHoles, jg::HoleKind::kImm64, imm),
                    jg::hole_patch(jg::kMovSlotImmHoles, jg::HoleKind::kDst32, *slot),
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
