/// @file jit_translate_movx.cpp
/// @brief SSE 16-byte move lowering (movups family), split out of
///        jit_translate.cpp for the file budget.
#include "jit_translate_movx.hpp"

#include <bit>
#include <cstdint>
#include <optional>

#include "jit_translate_mov.hpp"
#include <micro_forge/base/jit_templates_generated.hpp>

namespace micro_forge::base::detail {

using arch::x86_64::Mnemonic;
using arch::x86_64::Operand;

namespace jg = micro_forge::base::jit_gen;

std::optional<TranslatedInsn> translate_movx(const arch::x86_64::DecodedInsn& insn) {
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
            return op.kind == Operand::Kind::kMem && op.has_base && !op.rip_relative &&
                   op.disp >= INT32_MIN && op.disp <= INT32_MAX &&
                   (!op.has_index || (op.scale <= 8 && std::has_single_bit(op.scale)));
        };
        const bool to_xmm = xmm_slot(insn.ops[0]).has_value() && mem_ok(insn.ops[1]);
        const bool from_xmm = xmm_slot(insn.ops[1]).has_value() && mem_ok(insn.ops[0]);
        if (to_xmm || from_xmm) {
            const Operand& mem = to_xmm ? insn.ops[1] : insn.ops[0];
            const auto base = reg_slot_offset(mem.base);
            const auto xslot = xmm_slot(to_xmm ? insn.ops[0] : insn.ops[1]);
            if (mem.has_index && base.has_value() && xslot.has_value()) {
                const auto index = reg_slot_offset(mem.index);
                if (index.has_value()) {
                    const auto ishift = static_cast<std::uint64_t>(std::countr_zero(mem.scale));
                    const auto idisp = static_cast<std::uint32_t>(mem.disp);
                    return TranslatedInsn{
                        .code = jg::kMovxIdxSlot,
                        .patches =
                            {
                                jg::hole_patch(jg::kMovxIdxSlotHoles, jg::HoleKind::kBase32, *base),
                                jg::hole_patch(jg::kMovxIdxSlotHoles, jg::HoleKind::kIdx32, *index),
                                jg::hole_patch(jg::kMovxIdxSlotHoles, jg::HoleKind::kShift8, ishift),
                                jg::hole_patch(jg::kMovxIdxSlotHoles, jg::HoleKind::kDisp32, idisp),
                                jg::hole_patch(jg::kMovxIdxSlotHoles, jg::HoleKind::kSlot32, *xslot),
                            },
                        .call_offset =
                            jg::hole_patch(jg::kMovxIdxSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                        .cpu_offset =
                            jg::hole_patch(jg::kMovxIdxSlotHoles, jg::HoleKind::kCpu64, 0).offset,
                        .helper = to_xmm ? JitHelper::kLoadXmm : JitHelper::kStoreXmm,
                    };
                }
            }
            if (!mem.has_index && base.has_value() && xslot.has_value()) {
                const auto disp32 = static_cast<std::uint32_t>(mem.disp);
                // The two directions have different hole layouts, so each gets
                // its own patch list instead of a shared ternary.
                if (to_xmm) {
                    return TranslatedInsn{
                        .code = jg::kMovxLoadSlot,
                        .patches =
                            {
                                jg::hole_patch(jg::kMovxLoadSlotHoles, jg::HoleKind::kBase32, *base),
                                jg::hole_patch(jg::kMovxLoadSlotHoles, jg::HoleKind::kDisp32, disp32),
                                jg::hole_patch(jg::kMovxLoadSlotHoles, jg::HoleKind::kSlot32, *xslot),
                            },
                        .call_offset =
                            jg::hole_patch(jg::kMovxLoadSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                        .cpu_offset =
                            jg::hole_patch(jg::kMovxLoadSlotHoles, jg::HoleKind::kCpu64, 0).offset,
                        .helper = JitHelper::kLoadXmm,
                    };
                }
                return TranslatedInsn{
                    .code = jg::kMovxStoreSlot,
                    .patches =
                        {
                            jg::hole_patch(jg::kMovxStoreSlotHoles, jg::HoleKind::kBase32, *base),
                            jg::hole_patch(jg::kMovxStoreSlotHoles, jg::HoleKind::kDisp32, disp32),
                            jg::hole_patch(jg::kMovxStoreSlotHoles, jg::HoleKind::kSlot32, *xslot),
                        },
                    .call_offset =
                        jg::hole_patch(jg::kMovxStoreSlotHoles, jg::HoleKind::kHelper64, 0).offset,
                    .cpu_offset =
                        jg::hole_patch(jg::kMovxStoreSlotHoles, jg::HoleKind::kCpu64, 0).offset,
                    .helper = JitHelper::kStoreXmm,
                };
            }
        }
        return std::nullopt;
    }

    return std::nullopt;
}

} // namespace micro_forge::base::detail
