/**
 * @file jit_translate_alu.cpp
 * @brief Copy-and-patch lowering for the ALU family (split from
 *        jit_translate.cpp to respect the 600-line file budget).
 * @version 0.1.0
 */

#include "jit_translate_alu.hpp"

#include "jit_translate_mov.hpp"

#include "micro_forge/base/jit_templates_generated.hpp"

#include <bit>
#include <cstdint>
#include <vector>

namespace micro_forge::base::detail {

using arch::x86_64::Mnemonic;
using arch::x86_64::Operand;
using arch::x86_64::Reg;

JitHelper alu_helper(Mnemonic mnemonic) {
    switch (mnemonic) {
    case Mnemonic::kCmp:
        return JitHelper::kCmp;
    case Mnemonic::kTest:
        return JitHelper::kTest;
    case Mnemonic::kSub:
        return JitHelper::kSub;
    case Mnemonic::kAnd:
        return JitHelper::kAnd;
    case Mnemonic::kOr:
        return JitHelper::kOr;
    case Mnemonic::kXor:
        return JitHelper::kXor;
    case Mnemonic::kImul:
        return JitHelper::kImul;
    case Mnemonic::kBt:
        return JitHelper::kBt;
    case Mnemonic::kNot:
        return JitHelper::kNot;
    case Mnemonic::kNeg:
        return JitHelper::kNeg;
    case Mnemonic::kBts:
        return JitHelper::kBts;
    case Mnemonic::kBtr:
        return JitHelper::kBtr;
    case Mnemonic::kBtc:
        return JitHelper::kBtc;
    default:
        return JitHelper::kNone;
    }
}

namespace {

namespace jg = micro_forge::base::jit_gen;

/// Width gate: imul keeps the 4/8 forms (its CF/OF need mul_wide paths);
/// the rest of the family lowers every byte width the helpers mask.
bool alu_width(JitHelper helper, unsigned width) {
    if (width != 1 && width != 2 && width != 4 && width != 8) {
        return false;
    }
    return helper != JitHelper::kImul || width == 4 || width == 8;
}

// cmp reg,[base(+index*scale)+disp] tail: patches land on either kCmpMemSlot
// or kCmpMemIdxSlot; the index form stretches every later hole by 15 bytes.
std::optional<TranslatedInsn> cmp_mem(const arch::x86_64::DecodedInsn& insn, std::size_t slot) {
    const Operand& source = insn.ops[1];
    if (source.kind != Operand::Kind::kMem || source.size != insn.ops[0].size || !source.has_base ||
        source.rip_relative || insn.addr_size != 8 || insn.seg == arch::x86_64::Seg::kFs ||
        insn.seg == arch::x86_64::Seg::kGs || source.disp < INT32_MIN || source.disp > INT32_MAX) {
        return std::nullopt;
    }
    const auto base = reg_slot_offset(source.base);
    if (!base) {
        return std::nullopt;
    }
    // The indexed and plain forms are different templates with different
    // hole tables; `extra` used to shift every literal offset by 15.
    const auto cdisp = static_cast<std::uint32_t>(source.disp);
    const auto cwidth = static_cast<std::uint64_t>(insn.ops[0].size);
    if (source.has_index) {
        const auto index = reg_slot_offset(source.index);
        if (!index || source.scale > 8 || !std::has_single_bit(source.scale)) {
            return std::nullopt;
        }
        const auto cshift = static_cast<std::uint64_t>(std::countr_zero(source.scale));
        return TranslatedInsn{
            .code = jg::kCmpMemIdxSlot,
            .patches =
                {
                    jg::hole_patch(jg::kCmpMemIdxSlotHoles, jg::HoleKind::kBase32, *base),
                    jg::hole_patch(jg::kCmpMemIdxSlotHoles, jg::HoleKind::kIdx32, *index),
                    jg::hole_patch(jg::kCmpMemIdxSlotHoles, jg::HoleKind::kScale8, cshift),
                    jg::hole_patch(jg::kCmpMemIdxSlotHoles, jg::HoleKind::kDisp32, cdisp),
                    jg::hole_patch(jg::kCmpMemIdxSlotHoles, jg::HoleKind::kSlot32, slot),
                    jg::hole_patch(jg::kCmpMemIdxSlotHoles, jg::HoleKind::kWidth32, cwidth),
                },
            .call_offset =
                jg::hole_patch(jg::kCmpMemIdxSlotHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kCmpMemIdxSlotHoles, jg::HoleKind::kCpu64, 0).offset,
            .helper = JitHelper::kCmpMem,
        };
    }
    return TranslatedInsn{
        .code = jg::kCmpMemSlot,
        .patches =
            {
                jg::hole_patch(jg::kCmpMemSlotHoles, jg::HoleKind::kBase32, *base),
                jg::hole_patch(jg::kCmpMemSlotHoles, jg::HoleKind::kDisp32, cdisp),
                jg::hole_patch(jg::kCmpMemSlotHoles, jg::HoleKind::kSlot32, slot),
                jg::hole_patch(jg::kCmpMemSlotHoles, jg::HoleKind::kWidth32, cwidth),
            },
        .call_offset = jg::hole_patch(jg::kCmpMemSlotHoles, jg::HoleKind::kHelper64, 0).offset,
        .cpu_offset = jg::hole_patch(jg::kCmpMemSlotHoles, jg::HoleKind::kCpu64, 0).offset,
        .helper = JitHelper::kCmpMem,
    };
}

} // namespace

std::optional<TranslatedInsn> translate_alu_extra(const arch::x86_64::DecodedInsn& insn,
                                                  uint64_t rip) {
    // shift reg,cl: the template reads the cl slot at run time into rdx and
    // the existing jit_shift masks it exactly like the interpreter.
    if ((insn.mnemonic == Mnemonic::kShl || insn.mnemonic == Mnemonic::kShr ||
         insn.mnemonic == Mnemonic::kSar || insn.mnemonic == Mnemonic::kRol ||
         insn.mnemonic == Mnemonic::kRor || insn.mnemonic == Mnemonic::kRcl ||
         insn.mnemonic == Mnemonic::kRcr) &&
        insn.ops[0].kind == Operand::Kind::kReg && insn.ops[1].kind == Operand::Kind::kImplicit &&
        insn.addr_size == 8 && !insn.has_lock && insn.ops[0].size != 0) {
        const auto slot = reg_slot_offset(insn.ops[0].reg);
        const auto clslot = reg_slot_offset(Reg::kRcx);
        // Byte destinations must be slot-aligned: AH/CH/DH/BH live in the
        // upper byte and the helper addresses whole slots.
        if (slot.has_value() && clslot.has_value() && *slot % sizeof(std::uint64_t) == 0) {
            return TranslatedInsn{
                .code = jg::kShiftClSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kShiftClSlotHoles, jg::HoleKind::kSlot32, *slot),
                        jg::hole_patch(jg::kShiftClSlotHoles, jg::HoleKind::kClsl32, *clslot),
                        jg::hole_patch(jg::kShiftClSlotHoles, jg::HoleKind::kMnem32,
                                       static_cast<unsigned>(insn.mnemonic)),
                        jg::hole_patch(jg::kShiftClSlotHoles, jg::HoleKind::kWidth32,
                                       insn.ops[0].size),
                    },
                .call_offset =
                    jg::hole_patch(jg::kShiftClSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset = jg::hole_patch(jg::kShiftClSlotHoles, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kShiftCl,
            };
        }
    }
    // pxor xmm,xmm: one helper xors both 16-byte halves of the slots.
    if (insn.mnemonic == Mnemonic::kPxor && insn.ops[0].kind == Operand::Kind::kReg &&
        insn.ops[1].kind == Operand::Kind::kReg && insn.addr_size == 8 && !insn.has_lock &&
        static_cast<unsigned>(insn.ops[0].reg) >= 16U &&
        static_cast<unsigned>(insn.ops[1].reg) >= 16U) {
        const auto dst = static_cast<std::uint64_t>(static_cast<unsigned>(insn.ops[0].reg) - 16U);
        const auto src = static_cast<std::uint64_t>(static_cast<unsigned>(insn.ops[1].reg) - 16U);
        return TranslatedInsn{
            .code = jg::kPxorSlot,
            .patches =
                {
                    jg::hole_patch(jg::kPxorSlotHoles, jg::HoleKind::kDst32, dst * 16U),
                    jg::hole_patch(jg::kPxorSlotHoles, jg::HoleKind::kSrc32, src * 16U),
                },
            .call_offset = jg::hole_patch(jg::kPxorSlotHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kPxorSlotHoles, jg::HoleKind::kCpu64, 0).offset,
            .helper = JitHelper::kPxor,
        };
    }
    // Flag-only system instructions (cli/sti): the template carries just the
    // mnemonic; execute_system keeps the STI-shadow rule in one place.
    const bool operandless =
        insn.mnemonic == Mnemonic::kCli || insn.mnemonic == Mnemonic::kSti ||
        insn.mnemonic == Mnemonic::kClac || insn.mnemonic == Mnemonic::kStac ||
        insn.mnemonic == Mnemonic::kFninit || insn.mnemonic == Mnemonic::kRdmsr ||
        insn.mnemonic == Mnemonic::kWrmsr || insn.mnemonic == Mnemonic::kRdtsc ||
        insn.mnemonic == Mnemonic::kCpuid || insn.mnemonic == Mnemonic::kRdrand ||
        insn.mnemonic == Mnemonic::kPushfq || insn.mnemonic == Mnemonic::kPopfq;
    if (operandless && insn.ops[0].kind == Operand::Kind::kNone && insn.addr_size == 8 &&
        !insn.has_lock) {
        return TranslatedInsn{
            .code = jg::kPortsSlot,
            .patches =
                {
                    jg::hole_patch(jg::kPortsSlotHoles, jg::HoleKind::kMnem32,
                                   static_cast<unsigned>(insn.mnemonic)),
                    jg::hole_patch(jg::kPortsSlotHoles, jg::HoleKind::kWidth32, 8),
                },
            .call_offset = jg::hole_patch(jg::kPortsSlotHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kPortsSlotHoles, jg::HoleKind::kCpu64, 0).offset,
            .helper = JitHelper::kPorts,
        };
    }
    // in/out with the port in dx (EE/EC and the 16/32-bit variants): the
    // accumulator operand width comes from opsize, the port from rdx.
    if ((insn.mnemonic == Mnemonic::kIn || insn.mnemonic == Mnemonic::kOut) &&
        insn.addr_size == 8 && !insn.has_lock && insn.opsize != 0) {
        const auto port_op = insn.mnemonic == Mnemonic::kIn ? insn.ops[1] : insn.ops[0];
        const auto acc_op = insn.mnemonic == Mnemonic::kIn ? insn.ops[0] : insn.ops[1];
        if (port_op.kind == Operand::Kind::kImplicit && port_op.reg == Reg::kRdx &&
            acc_op.reg == Reg::kRax) {
            return TranslatedInsn{
                .code = jg::kPortsSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kPortsSlotHoles, jg::HoleKind::kMnem32,
                                       static_cast<unsigned>(insn.mnemonic)),
                        jg::hole_patch(jg::kPortsSlotHoles, jg::HoleKind::kWidth32, insn.opsize),
                    },
                .call_offset = 23,
                .cpu_offset = 13,
                .helper = JitHelper::kPorts,
            };
        }
    }
    // test/cmp [base+index*scale+disp],src: flag-only, so one template with
    // the SIB address arithmetic serves both source kinds.
    if (insn.ops[0].kind == Operand::Kind::kMem && insn.ops[0].has_base && insn.ops[0].has_index &&
        !insn.ops[0].rip_relative && insn.ops[0].size == insn.opsize && insn.addr_size == 8 &&
        !insn.has_lock && insn.seg != arch::x86_64::Seg::kFs &&
        insn.seg != arch::x86_64::Seg::kGs &&
        ((insn.mnemonic == Mnemonic::kTest && insn.ops[1].kind == Operand::Kind::kReg) ||
         (insn.mnemonic == Mnemonic::kCmp && insn.ops[1].kind == Operand::Kind::kImm))) {
        const auto base = reg_slot_offset(insn.ops[0].base);
        const auto index = reg_slot_offset(insn.ops[0].index);
        const auto& mem = insn.ops[0];
        const auto src_slot = operand_slot(insn.ops[1], insn.rex_present);
        if (base.has_value() && index.has_value() && src_slot.has_value() && mem.scale <= 8 &&
            std::has_single_bit(mem.scale)) {
            const bool is_test = insn.mnemonic == Mnemonic::kTest;
            const auto src = is_test ? *src_slot : static_cast<std::size_t>(insn.ops[1].disp);
            const auto shift = static_cast<std::uint64_t>(std::countr_zero(mem.scale));
            return TranslatedInsn{
                .code = jg::kAluMemIdxSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kAluMemIdxSlotHoles, jg::HoleKind::kBase32, *base),
                        jg::hole_patch(jg::kAluMemIdxSlotHoles, jg::HoleKind::kIdx32, *index),
                        jg::hole_patch(jg::kAluMemIdxSlotHoles, jg::HoleKind::kShift8, shift),
                        jg::hole_patch(jg::kAluMemIdxSlotHoles, jg::HoleKind::kDisp32,
                                       static_cast<std::uint32_t>(mem.disp)),
                        jg::hole_patch(jg::kAluMemIdxSlotHoles, jg::HoleKind::kSrc32, src),
                        jg::hole_patch(jg::kAluMemIdxSlotHoles, jg::HoleKind::kMnem32,
                                       static_cast<unsigned>(insn.mnemonic)),
                        jg::hole_patch(jg::kAluMemIdxSlotHoles, jg::HoleKind::kWidth32,
                                       insn.ops[0].size),
                    },
                .call_offset =
                    jg::hole_patch(jg::kAluMemIdxSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset =
                    jg::hole_patch(jg::kAluMemIdxSlotHoles, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kAluMemIdx,
            };
        }
    }
    // lea r32,[base+disp]: 32-bit arithmetic, whole-slot write zero-extends.
    if (insn.mnemonic == Mnemonic::kLea && insn.ops[1].kind == Operand::Kind::kMem &&
        insn.ops[1].has_base && !insn.ops[1].has_index && !insn.ops[1].rip_relative &&
        insn.ops[0].size == 4 && insn.addr_size == 8 && !insn.has_lock &&
        insn.ops[1].disp >= INT32_MIN && insn.ops[1].disp <= INT32_MAX) {
        const auto base = reg_slot_offset(insn.ops[1].base);
        const auto dst = operand_slot(insn.ops[0], insn.rex_present);
        const auto ldisp = static_cast<std::uint32_t>(insn.ops[1].disp);
        if (base.has_value() && dst.has_value()) {
            return TranslatedInsn{
                .code = jg::kLeaSlot32,
                .patches =
                    {
                        jg::hole_patch(jg::kLeaSlot32Holes, jg::HoleKind::kBase32, *base),
                        jg::hole_patch(jg::kLeaSlot32Holes, jg::HoleKind::kDisp32, ldisp),
                        jg::hole_patch(jg::kLeaSlot32Holes, jg::HoleKind::kDst32, *dst),
                    },
                .call_offset = std::nullopt,
                .cpu_offset = std::nullopt,
                .helper = JitHelper::kNone,
            };
        }
    }
    // lea reg,[rip+disp]: the result is rip+length+disp, a compile-time
    // constant, so the host-only movabs template writes it directly.
    if (insn.mnemonic == Mnemonic::kLea && insn.ops[1].kind == Operand::Kind::kMem &&
        insn.ops[1].rip_relative && insn.ops[0].kind == Operand::Kind::kReg &&
        insn.ops[0].size == 8 && insn.addr_size == 8 && !insn.has_lock) {
        const auto slot = detail::operand_slot(insn.ops[0], insn.rex_present);
        if (slot.has_value()) {
            const uint64_t value = rip + insn.length + static_cast<uint64_t>(insn.ops[1].disp);
            return TranslatedInsn{
                .code = jg::kMovAbsSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kMovAbsSlotHoles, jg::HoleKind::kImm64, value),
                        jg::hole_patch(jg::kMovAbsSlotHoles, jg::HoleKind::kDst32, *slot),
                    },
                .call_offset = std::nullopt,
                .cpu_offset = std::nullopt,
                .helper = JitHelper::kNone,
            };
        }
    }
    // imul dst,src,imm (0F AF with an immediate): execute_imul23 owns the
    // CF/OF overflow rule, so the helper rebuilds its shape as well.
    if (insn.mnemonic == Mnemonic::kImul && insn.ops[0].kind == Operand::Kind::kReg &&
        insn.ops[1].kind == Operand::Kind::kReg && insn.ops[2].kind == Operand::Kind::kImm &&
        insn.addr_size == 8 && !insn.has_lock && (insn.opsize == 4 || insn.opsize == 8)) {
        const auto dst = operand_slot(insn.ops[0], insn.rex_present);
        const auto src = reg_slot_offset(insn.ops[1].reg);
        const auto immv = static_cast<std::uint64_t>(insn.ops[2].disp);
        if (dst.has_value() && src.has_value()) {
            return TranslatedInsn{
                .code = jg::kImul23Slot,
                .patches =
                    {
                        jg::hole_patch(jg::kImul23SlotHoles, jg::HoleKind::kDst32, *dst),
                        jg::hole_patch(jg::kImul23SlotHoles, jg::HoleKind::kSrc32, *src),
                        jg::hole_patch(jg::kImul23SlotHoles, jg::HoleKind::kImm64, immv),
                        jg::hole_patch(jg::kImul23SlotHoles, jg::HoleKind::kWidth32, insn.opsize),
                    },
                .call_offset =
                    jg::hole_patch(jg::kImul23SlotHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset = jg::hole_patch(jg::kImul23SlotHoles, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kImul23,
            };
        }
    }
    // lgdt/lidt/fxsave take a memory operand; the template computes the
    // address and the helper rebuilds the operand shape around it.
    if ((insn.mnemonic == Mnemonic::kLgdt || insn.mnemonic == Mnemonic::kLidt ||
         insn.mnemonic == Mnemonic::kSgdt || insn.mnemonic == Mnemonic::kSidt ||
         insn.mnemonic == Mnemonic::kFxsave || insn.mnemonic == Mnemonic::kFxrstor) &&
        insn.ops[0].kind == Operand::Kind::kMem && insn.ops[0].has_base && !insn.ops[0].has_index &&
        !insn.ops[0].rip_relative && insn.addr_size == 8 && !insn.has_lock &&
        insn.ops[0].disp >= INT32_MIN && insn.ops[0].disp <= INT32_MAX) {
        const auto base = reg_slot_offset(insn.ops[0].base);
        const auto sdisp = static_cast<std::uint32_t>(insn.ops[0].disp);
        const auto smn = static_cast<unsigned>(insn.mnemonic);
        if (base.has_value()) {
            return TranslatedInsn{
                .code = jg::kGrp3MemSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kGrp3MemSlotHoles, jg::HoleKind::kBase32, *base),
                        jg::hole_patch(jg::kGrp3MemSlotHoles, jg::HoleKind::kDisp32, sdisp),
                        jg::hole_patch(jg::kGrp3MemSlotHoles, jg::HoleKind::kWidth32,
                                       insn.ops[0].size),
                        jg::hole_patch(jg::kGrp3MemSlotHoles, jg::HoleKind::kMnem32, smn),
                    },
                .call_offset =
                    jg::hole_patch(jg::kGrp3MemSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset = jg::hole_patch(jg::kGrp3MemSlotHoles, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kSysMem,
            };
        }
    }
    // one-operand grp3 with a memory divisor: read then divide.
    if ((insn.mnemonic == Mnemonic::kMul || insn.mnemonic == Mnemonic::kImul ||
         insn.mnemonic == Mnemonic::kDiv || insn.mnemonic == Mnemonic::kIdiv) &&
        insn.ops[0].kind == Operand::Kind::kMem && insn.ops[1].kind == Operand::Kind::kNone &&
        insn.ops[0].has_base && !insn.ops[0].has_index && !insn.ops[0].rip_relative &&
        insn.ops[0].size == insn.opsize && insn.addr_size == 8 && !insn.has_lock &&
        insn.seg != arch::x86_64::Seg::kFs && insn.seg != arch::x86_64::Seg::kGs &&
        insn.ops[0].disp >= INT32_MIN && insn.ops[0].disp <= INT32_MAX) {
        const auto base = reg_slot_offset(insn.ops[0].base);
        const auto gdisp = static_cast<std::uint32_t>(insn.ops[0].disp);
        const auto gmn = static_cast<unsigned>(insn.mnemonic);
        if (base.has_value()) {
            return TranslatedInsn{
                .code = jg::kGrp3MemSlot,
                .patches =
                    {
                        jg::hole_patch(jg::kGrp3MemSlotHoles, jg::HoleKind::kBase32, *base),
                        jg::hole_patch(jg::kGrp3MemSlotHoles, jg::HoleKind::kDisp32, gdisp),
                        jg::hole_patch(jg::kGrp3MemSlotHoles, jg::HoleKind::kWidth32,
                                       insn.ops[0].size),
                        jg::hole_patch(jg::kGrp3MemSlotHoles, jg::HoleKind::kMnem32, gmn),
                    },
                .call_offset =
                    jg::hole_patch(jg::kGrp3MemSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset = jg::hole_patch(jg::kGrp3MemSlotHoles, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kGrp3Mem,
            };
        }
    }
    // one-operand mul/imul/div/idiv r64..r8 (F6/F7 group 3): the helper
    // rebuilds the instruction shape and runs execute_grp3, so the rdx:rax
    // split, flag updates and #DE latching stay in one implementation.
    if ((insn.mnemonic == Mnemonic::kMul || insn.mnemonic == Mnemonic::kImul ||
         insn.mnemonic == Mnemonic::kDiv || insn.mnemonic == Mnemonic::kIdiv) &&
        insn.ops[0].kind == Operand::Kind::kReg && insn.ops[1].kind == Operand::Kind::kNone &&
        insn.addr_size == 8 && !insn.has_lock &&
        (insn.opsize == 1 || insn.opsize == 2 || insn.opsize == 4 || insn.opsize == 8)) {
        const auto src = reg_slot_offset(insn.ops[0].reg);
        if (src.has_value()) {
            return TranslatedInsn{
                .code = jg::kGrp3Slot,
                .patches =
                    {
                        jg::hole_patch(jg::kGrp3SlotHoles, jg::HoleKind::kSrc32, *src),
                        jg::hole_patch(jg::kGrp3SlotHoles, jg::HoleKind::kWidth32, insn.opsize),
                        jg::hole_patch(jg::kGrp3SlotHoles, jg::HoleKind::kMnem32,
                                       static_cast<unsigned>(insn.mnemonic)),
                    },
                .call_offset =
                    jg::hole_patch(jg::kGrp3SlotHoles, jg::HoleKind::kHelper64, 0).offset,
                .cpu_offset = jg::hole_patch(jg::kGrp3SlotHoles, jg::HoleKind::kCpu64, 0).offset,
                .helper = JitHelper::kGrp3,
            };
        }
    }
    return std::nullopt;
}

std::optional<TranslatedInsn> translate_alu(const arch::x86_64::DecodedInsn& insn, std::size_t slot,
                                            JitHelper helper) {
    const unsigned width = insn.ops[0].size;
    const Operand& source = insn.ops[1];
    // AH/CH/DH/BH map to byte 1 of their parent slot; the helper ABI addresses
    // whole slots, so misaligned byte slots must stay with the interpreter.
    if (slot % sizeof(uint64_t) != 0) {
        return std::nullopt;
    }
    // Three-operand imul (reg, r/m, imm) stays with the interpreter.
    if (insn.has_lock || !alu_width(helper, width) ||
        (helper == JitHelper::kImul && insn.ops[2].kind != Operand::Kind::kNone)) {
        return std::nullopt;
    }
    // bt reg,imm: the immediate is the bit index, which is exactly what
    // jit_bt's second argument already is.
    if ((helper == JitHelper::kBt || helper == JitHelper::kBts || helper == JitHelper::kBtr ||
         helper == JitHelper::kBtc) &&
        source.kind == Operand::Kind::kImm) {
        return TranslatedInsn{
            .code = jg::kAluImmHelper,
            .patches =
                {
                    jg::hole_patch(jg::kAluImmHelperHoles, jg::HoleKind::kSlot32, slot),
                    jg::hole_patch(jg::kAluImmHelperHoles, jg::HoleKind::kImm64,
                                   static_cast<uint64_t>(source.disp)),
                    jg::hole_patch(jg::kAluImmHelperHoles, jg::HoleKind::kWidth32, width),
                },
            .call_offset =
                jg::hole_patch(jg::kAluImmHelperHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kAluImmHelperHoles, jg::HoleKind::kCpu64, 0).offset,
            .helper = helper,
        };
    }
    if (source.kind == Operand::Kind::kImm && source.disp >= INT32_MIN &&
        source.disp <= (width == 4 ? static_cast<int64_t>(UINT32_MAX) : INT32_MAX)) {
        return TranslatedInsn{
            .code = jg::kAluImmHelper,
            .patches =
                {
                    jg::hole_patch(jg::kAluImmHelperHoles, jg::HoleKind::kSlot32, slot),
                    jg::hole_patch(jg::kAluImmHelperHoles, jg::HoleKind::kImm64,
                                   static_cast<uint64_t>(source.disp)),
                    jg::hole_patch(jg::kAluImmHelperHoles, jg::HoleKind::kWidth32, width),
                },
            .call_offset =
                jg::hole_patch(jg::kAluImmHelperHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kAluImmHelperHoles, jg::HoleKind::kCpu64, 0).offset,
            .helper = helper,
        };
    }
    // Unary forms (not, neg) have no second operand; the shared template
    // carries the slot twice and the helper ignores the extra one.
    // template carries the slot twice and the helper ignores the extra one.
    if ((helper == JitHelper::kNot || helper == JitHelper::kNeg) &&
        source.kind == Operand::Kind::kNone) {
        return TranslatedInsn{
            .code = jg::kAluRegHelper,
            .patches =
                {
                    jg::hole_patch(jg::kAluRegHelperHoles, jg::HoleKind::kSlot32, slot),
                    jg::hole_patch(jg::kAluRegHelperHoles, jg::HoleKind::kSrc32, slot),
                    jg::hole_patch(jg::kAluRegHelperHoles, jg::HoleKind::kWidth32, width),
                },
            .call_offset =
                jg::hole_patch(jg::kAluRegHelperHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kAluRegHelperHoles, jg::HoleKind::kCpu64, 0).offset,
            .helper = helper,
        };
    }
    if (source.kind == Operand::Kind::kReg && source.size == width) {
        const auto src_slot = reg_slot_offset(source.reg);
        if (!src_slot) {
            return std::nullopt;
        }
        return TranslatedInsn{
            .code = jg::kAluRegHelper,
            .patches =
                {
                    jg::hole_patch(jg::kAluRegHelperHoles, jg::HoleKind::kSlot32, slot),
                    jg::hole_patch(jg::kAluRegHelperHoles, jg::HoleKind::kSrc32, *src_slot),
                    jg::hole_patch(jg::kAluRegHelperHoles, jg::HoleKind::kWidth32, width),
                },
            .call_offset =
                jg::hole_patch(jg::kAluRegHelperHoles, jg::HoleKind::kHelper64, 0).offset,
            .cpu_offset = jg::hole_patch(jg::kAluRegHelperHoles, jg::HoleKind::kCpu64, 0).offset,
            .helper = helper,
        };
    }
    if (helper == JitHelper::kCmp) {
        return cmp_mem(insn, slot);
    }
    return std::nullopt;
}

} // namespace micro_forge::base::detail
