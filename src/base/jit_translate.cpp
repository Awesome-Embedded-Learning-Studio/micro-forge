/**
 * @file jit_translate.cpp
 * @brief Copy-and-patch lowering for the first instruction slice.
 * @version 0.1.0
 */

#include "micro_forge/base/jit_translate.hpp"

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
    default:
        return JitHelper::kNone;
    }
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
    const auto helper = alu_helper(insn.mnemonic);
    if (helper != JitHelper::kNone) {
        const auto width = insn.ops[0].size;
        const auto& source = insn.ops[1];
        if ((width != 4 && width != 8) || insn.has_lock) {
            return std::nullopt;
        }
        if (source.kind == Operand::Kind::kImm && source.disp >= INT32_MIN &&
            source.disp <= (width == 4 ? static_cast<int64_t>(UINT32_MAX) : INT32_MAX)) {
            return TranslatedInsn{
                .code = kAluImmHelper,
                .patches =
                    {
                        {.offset = 2, .size = 4, .value = *slot},
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
                        {.offset = 2, .size = 4, .value = *slot},
                        {.offset = 10, .size = 4, .value = *src_slot},
                        {.offset = 15, .size = 4, .value = width},
                    },
                .call_offset = 31,
                .cpu_offset = 21,
                .helper = helper,
            };
        }
        if (helper != JitHelper::kCmp || source.kind != Operand::Kind::kMem ||
            source.size != width || !source.has_base || source.rip_relative ||
            insn.addr_size != 8 || insn.seg == arch::x86_64::Seg::kFs ||
            insn.seg == arch::x86_64::Seg::kGs || source.disp < INT32_MIN ||
            source.disp > INT32_MAX) {
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
        patches.push_back({.offset = 17 + extra, .size = 4, .value = *slot});
        patches.push_back({.offset = 22 + extra, .size = 4, .value = width});
        return TranslatedInsn{
            .code = source.has_index ? std::span<const uint8_t>(kCmpMemIdxSlot) : kCmpMemSlot,
            .patches = std::move(patches),
            .call_offset = 38 + extra,
            .cpu_offset = 28 + extra,
            .helper = JitHelper::kCmpMem,
        };
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
    // lea dst,[base+index*scale+disp]: host-side address arithmetic only.
    if (insn.mnemonic == Mnemonic::kLea && insn.ops[1].kind == Operand::Kind::kMem &&
        insn.ops[1].has_base && insn.ops[1].has_index && !insn.ops[1].rip_relative &&
        insn.ops[0].size == 8) {
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
                .offset = 33,
                .size = 4,
                .value = *slot,
            },
        };
        return TranslatedInsn{
            .code = kLeaIdxSlot,
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
