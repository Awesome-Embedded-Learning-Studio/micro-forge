/**
 * @file jit_translate_alu.cpp
 * @brief Copy-and-patch lowering for the ALU family (split from
 *        jit_translate.cpp to respect the 600-line file budget).
 * @version 0.1.0
 */

#include "jit_translate_alu.hpp"

#include "jit_translate_mov.hpp"

#include "micro_forge/base/jit_templates.hpp"

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
    default:
        return JitHelper::kNone;
    }
}

namespace {

using namespace jit_templates;

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
    std::vector<JitPage::Patch> patches = {{.offset = 5, .size = 4, .value = *base}};
    const std::size_t extra = source.has_index ? 15 : 0;
    if (source.has_index) {
        const auto index = reg_slot_offset(source.index);
        if (!index || source.scale > 8 || !std::has_single_bit(source.scale)) {
            return std::nullopt;
        }
        patches.push_back({.offset = 13, .size = 4, .value = *index});
        patches.push_back({
            .offset = 20,
            .size = 1,
            .value = static_cast<unsigned>(std::countr_zero(source.scale)),
        });
    }
    patches.push_back(
        {.offset = 12 + extra, .size = 4, .value = static_cast<uint32_t>(source.disp)});
    patches.push_back({.offset = 17 + extra, .size = 4, .value = slot});
    patches.push_back({.offset = 22 + extra, .size = 4, .value = insn.ops[0].size});
    return TranslatedInsn{
        .code = source.has_index ? std::span<const uint8_t>(kCmpMemIdxSlot) : kCmpMemSlot,
        .patches = std::move(patches),
        .call_offset = 38 + extra,
        .cpu_offset = 28 + extra,
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
        insn.addr_size == 8 && !insn.has_lock &&
        (insn.ops[0].size == 2 || insn.ops[0].size == 4 || insn.ops[0].size == 8)) {
        const auto slot = reg_slot_offset(insn.ops[0].reg);
        const auto clslot = reg_slot_offset(Reg::kRcx);
        if (slot.has_value() && clslot.has_value()) {
            return TranslatedInsn{
                .code = jit_templates::kShiftClSlot,
                .patches =
                    {
                        {.offset = 2, .size = 4, .value = *slot},
                        {.offset = 9, .size = 4, .value = *clslot},
                        {.offset = 14, .size = 4, .value = static_cast<unsigned>(insn.mnemonic)},
                        {.offset = 20, .size = 4, .value = insn.ops[0].size},
                    },
                .call_offset = 36,
                .cpu_offset = 26,
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
            .code = jit_templates::kPxorSlot,
            .patches =
                {
                    {.offset = 2, .size = 4, .value = dst * 16U},
                    {.offset = 7, .size = 4, .value = src * 16U},
                },
            .call_offset = 23,
            .cpu_offset = 13,
            .helper = JitHelper::kPxor,
        };
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
                .code = jit_templates::kLeaSlot32,
                .patches =
                    {
                        {.offset = 3, .size = 4, .value = *base},
                        {.offset = 8, .size = 4, .value = ldisp},
                        {.offset = 16, .size = 4, .value = *dst},
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
                .code = jit_templates::kMovAbsSlot,
                .patches =
                    {
                        {.offset = 2, .size = 8, .value = value},
                        {.offset = 13, .size = 4, .value = *slot},
                    },
                .call_offset = std::nullopt,
                .cpu_offset = std::nullopt,
                .helper = JitHelper::kNone,
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
    if (source.kind == Operand::Kind::kImm && source.disp >= INT32_MIN &&
        source.disp <= (width == 4 ? static_cast<int64_t>(UINT32_MAX) : INT32_MAX)) {
        return TranslatedInsn{
            .code = kAluImmHelper,
            .patches =
                {
                    {.offset = 2, .size = 4, .value = slot},
                    {.offset = 8, .size = 8, .value = static_cast<uint64_t>(source.disp)},
                    {.offset = 17, .size = 4, .value = width},
                },
            .call_offset = 33,
            .cpu_offset = 23,
            .helper = helper,
        };
    }
    if (source.kind == Operand::Kind::kReg && source.size == width) {
        const auto src_slot = reg_slot_offset(source.reg);
        if (!src_slot) {
            return std::nullopt;
        }
        return TranslatedInsn{
            .code = kAluRegHelper,
            .patches =
                {
                    {.offset = 2, .size = 4, .value = slot},
                    {.offset = 10, .size = 4, .value = *src_slot},
                    {.offset = 15, .size = 4, .value = width},
                },
            .call_offset = 31,
            .cpu_offset = 21,
            .helper = helper,
        };
    }
    if (helper == JitHelper::kCmp) {
        return cmp_mem(insn, slot);
    }
    return std::nullopt;
}

} // namespace micro_forge::base::detail
