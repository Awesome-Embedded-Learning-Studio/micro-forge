/**
 * @file jit_translate.cpp
 * @brief Copy-and-patch lowering for the first instruction slice.
 * @version 0.1.0
 */

#include "micro_forge/base/jit_translate.hpp"

#include "jit_translate_alu.hpp"

#include "jit_translate_mov.hpp"

#include "micro_forge/base/jit_templates.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>

namespace micro_forge::base {

namespace {

using arch::x86_64::Mnemonic;
using arch::x86_64::Operand;
using arch::x86_64::Reg;

using namespace jit_templates;

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
            .code = jit_templates::kNopJit,
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
                .code = jit_templates::kRetHelper,
                .patches =
                    {
                        JitPage::Patch{.offset = 5, .size = 4, .value = *rsp_slot},
                        JitPage::Patch{.offset = 11, .size = 8, .value = rip},
                    },
                .call_offset = 31,
                .cpu_offset = 21,
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
                .code = jit_templates::kCallImmHelper,
                .patches =
                    {
                        JitPage::Patch{.offset = 3, .size = 8, .value = target},
                        JitPage::Patch{.offset = 13, .size = 8, .value = rip},
                        JitPage::Patch{.offset = 23, .size = 8, .value = ret_addr},
                    },
                .call_offset = 43,
                .cpu_offset = 33,
                .helper = JitHelper::kCall,
            };
        }
        if (insn.ops[0].kind == Operand::Kind::kReg && insn.ops[0].size == 8) {
            const auto reg = reg_slot_offset(insn.ops[0].reg);
            const uint64_t ret_addr = rip + insn.length;
            if (reg.has_value()) {
                return TranslatedInsn{
                    .code = jit_templates::kCallRegHelper,
                    .patches =
                        {
                            JitPage::Patch{.offset = 5, .size = 4, .value = *reg},
                            JitPage::Patch{.offset = 11, .size = 8, .value = rip},
                            JitPage::Patch{.offset = 21, .size = 8, .value = ret_addr},
                        },
                    .call_offset = 41,
                    .cpu_offset = 31,
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
                .code = jit_templates::kShiftImmSlot,
                .patches =
                    {
                        {.offset = 2, .size = 4, .value = *slot},
                        {.offset = 7, .size = 4, .value = count},
                        {.offset = 12, .size = 4, .value = mn},
                        {.offset = 18, .size = 4, .value = insn.ops[0].size},
                    },
                .call_offset = 34,
                .cpu_offset = 24,
                .helper = JitHelper::kShift,
            };
        }
    }
    // jmp imm: record the patched target; the host ret ends the block.
    if (insn.mnemonic == Mnemonic::kJmp && insn.addr_size == 8 && !insn.has_lock &&
        insn.ops[0].kind == Operand::Kind::kImm) {
        const uint64_t target = rip + insn.length + static_cast<uint64_t>(insn.ops[0].disp);
        return TranslatedInsn{
            .code = jit_templates::kJmpImmHelper,
            .patches =
                {
                    JitPage::Patch{.offset = 3, .size = 8, .value = target},
                },
            .call_offset = 23,
            .cpu_offset = 13,
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
            .code = jit_templates::kJccHelper,
            .patches =
                {
                    JitPage::Patch{.offset = 3, .size = 8, .value = taken},
                    JitPage::Patch{.offset = 13, .size = 8, .value = fall},
                    JitPage::Patch{.offset = 22, .size = 4, .value = cc},
                },
            .call_offset = 38,
            .cpu_offset = 28,
            .helper = JitHelper::kJcc,
        };
    }
    // leave: rsp=rbp then pop rbp; helper-faulted reads latch like stores.
    if (insn.mnemonic == Mnemonic::kLeave && insn.addr_size == 8 && !insn.has_lock &&
        insn.ops[0].kind != Operand::Kind::kImm) {
        return TranslatedInsn{
            .code = jit_templates::kLeaveHelper,
            .patches = {},
            .call_offset = 13,
            .cpu_offset = 3,
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
    // mov family lives in jit_translate_mov.cpp: store reg/imm, load reg.
    if (auto moved = detail::translate_move(insn, rip)) {
        return moved;
    }
    // push imm: the value is already sign-extended into ops[0].disp.
    if (insn.addr_size == 8 && !insn.has_lock && insn.mnemonic == Mnemonic::kPush &&
        insn.ops[0].kind == Operand::Kind::kImm) {
        const JitPage::Patch imm_patch{
            .offset = 3,
            .size = 8,
            .value = static_cast<std::uint64_t>(insn.ops[0].disp),
        };
        return TranslatedInsn{
            .code = jit_templates::kPushImmHelper,
            .patches = {imm_patch},
            .call_offset = 23,
            .cpu_offset = 13,
            .helper = JitHelper::kPushImm,
        };
    }
    // movups/movaps/movdqa/movdqu xmm <-> [base+disp]: 16-byte SSE moves
    // through one helper that walks both qword halves.
    if (insn.addr_size == 8 && !insn.has_lock && insn.rep == arch::x86_64::Rep::kNone &&
        (insn.mnemonic == Mnemonic::kMovups || insn.mnemonic == Mnemonic::kMovaps ||
         insn.mnemonic == Mnemonic::kMovdqa || insn.mnemonic == Mnemonic::kMovdqu) &&
        insn.seg != arch::x86_64::Seg::kFs && insn.seg != arch::x86_64::Seg::kGs) {
        const auto xmm_slot = [](const Operand& op) -> std::optional<std::size_t> {
            const auto reg = static_cast<unsigned>(op.reg);
            if (op.kind != Operand::Kind::kReg || reg < 16U || reg > 31U) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(reg - 16U) * 16U;
        };
        const auto mem_ok = [&](const Operand& op) {
            return op.kind == Operand::Kind::kMem && op.has_base && !op.has_index &&
                   !op.rip_relative && op.disp >= INT32_MIN && op.disp <= INT32_MAX;
        };
        const bool to_xmm = xmm_slot(insn.ops[0]).has_value() && mem_ok(insn.ops[1]);
        const bool from_xmm = xmm_slot(insn.ops[1]).has_value() && mem_ok(insn.ops[0]);
        if (to_xmm || from_xmm) {
            const Operand& mem = to_xmm ? insn.ops[1] : insn.ops[0];
            const auto base = reg_slot_offset(mem.base);
            const auto xslot = xmm_slot(to_xmm ? insn.ops[0] : insn.ops[1]);
            if (base.has_value() && xslot.has_value()) {
                std::vector<JitPage::Patch> patches = {
                    JitPage::Patch{
                        .offset = 5,
                        .size = 4,
                        .value = *base,
                    },
                    JitPage::Patch{
                        .offset = 12,
                        .size = 4,
                        .value = static_cast<std::uint32_t>(mem.disp),
                    },
                };
                patches.push_back(JitPage::Patch{
                    .offset = to_xmm ? 27U : 17U,
                    .size = 4,
                    .value = *xslot,
                });
                return TranslatedInsn{
                    .code = to_xmm ? jit_templates::kMovxLoadSlot : jit_templates::kMovxStoreSlot,
                    .patches = std::move(patches),
                    .call_offset = 33,
                    .cpu_offset = to_xmm ? 18 : 23,
                    .helper = to_xmm ? JitHelper::kLoadXmm : JitHelper::kStoreXmm,
                };
            }
        }
        return std::nullopt;
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
                .code = jit_templates::kAluRegSlot,
                .patches =
                    {
                        {.offset = 2, .size = 4, .value = *slot},
                        {.offset = 7, .size = 4, .value = *src_slot},
                        {.offset = 12, .size = 4, .value = static_cast<unsigned>(insn.mnemonic)},
                        {.offset = 18, .size = 4, .value = insn.ops[0].size},
                    },
                .call_offset = 34,
                .cpu_offset = 24,
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
                .code = jit_templates::kCmovRegHelper,
                .patches =
                    {
                        {.offset = 2, .size = 4, .value = *slot},
                        {.offset = 10, .size = 4, .value = *src_slot},
                        {.offset = 15, .size = 4, .value = static_cast<std::uint32_t>(insn.cc)},
                        {.offset = 21, .size = 4, .value = insn.ops[0].size},
                    },
                .call_offset = 37,
                .cpu_offset = 27,
                .helper = JitHelper::kCmov,
            };
        }
        return std::nullopt;
    }
    // setcc reg8: single-byte conditional write, flags come from the helper.
    if (insn.mnemonic == Mnemonic::kSetcc && *slot % sizeof(uint64_t) == 0 &&
        insn.ops[0].kind == Operand::Kind::kReg && insn.ops[0].size == 1 && !insn.has_lock) {
        return TranslatedInsn{
            .code = jit_templates::kSetccRegHelper,
            .patches =
                {
                    {.offset = 2, .size = 4, .value = *slot},
                    {.offset = 7, .size = 4, .value = static_cast<std::uint32_t>(insn.cc)},
                },
            .call_offset = 23,
            .cpu_offset = 13,
            .helper = JitHelper::kSetcc,
        };
    }
    // push/pop reg64 and push imm: the helper walks rsp via push_value/pop_value.
    if (insn.addr_size == 8 && !insn.has_lock) {
        if (insn.mnemonic == Mnemonic::kPush && insn.ops[0].kind == Operand::Kind::kReg &&
            insn.ops[0].size == 8 && *slot % sizeof(uint64_t) == 0) {
            return TranslatedInsn{
                .code = jit_templates::kPushRegHelper,
                .patches = {{.offset = 5, .size = 4, .value = *slot}},
                .call_offset = 21,
                .cpu_offset = 11,
                .helper = JitHelper::kPushVal,
            };
        }
        if (insn.mnemonic == Mnemonic::kPop && insn.ops[0].kind == Operand::Kind::kReg &&
            insn.ops[0].size == 8 && *slot % sizeof(uint64_t) == 0) {
            return TranslatedInsn{
                .code = jit_templates::kPopRegHelper,
                .patches = {{.offset = 2, .size = 4, .value = *slot}},
                .call_offset = 18,
                .cpu_offset = 8,
                .helper = JitHelper::kPopReg,
            };
        }
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
        std::vector<JitPage::Patch> patches = {
            JitPage::Patch{.offset = 4, .size = 4, .value = *base},
            JitPage::Patch{.offset = 12, .size = 4, .value = *index},
            JitPage::Patch{.offset = 19, .size = 1, .value = scale8},
            JitPage::Patch{
                .offset = 25,
                .size = 4,
                .value = static_cast<std::uint32_t>(insn.ops[1].disp),
            },
            JitPage::Patch{
                .offset = wide ? std::size_t{33} : std::size_t{35},
                .size = 4,
                .value = *slot,
            },
        };
        return TranslatedInsn{
            .code = wide ? std::span<const uint8_t>(kLeaIdxSlot) : kLeaIdxSlot32,
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
            JitPage::Patch{.offset = 4, .size = 4, .value = *source},
            JitPage::Patch{.offset = 12, .size = 4, .value = *slot},
        };
        return TranslatedInsn{
            .code = kMovSlotSlot,
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
            JitPage::Patch{.offset = 3, .size = 4, .value = *source},
            JitPage::Patch{.offset = 11, .size = 4, .value = *slot},
        };
        return TranslatedInsn{
            .code = kMovSlotSlot32,
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
