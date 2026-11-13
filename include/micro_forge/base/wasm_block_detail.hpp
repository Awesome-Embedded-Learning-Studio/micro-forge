/**
 * @file wasm_block_detail.hpp
 * @brief Shared emission primitives and layout constants for wasm blocks.
 * @version 0.1.0
 *
 * Split out of wasm_block.cpp so the memory-operand lowerer (wasm_block_mem.cpp)
 * and the register lowerer share one source of truth for opcodes, scratch
 * locals and the GPR mirror layout.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "micro_forge/arch/x86_64/decoded.hpp"
#include "micro_forge/base/wasm_emit.hpp"

namespace micro_forge::base::detail {

using arch::x86_64::Mnemonic;
using arch::x86_64::Operand;

// wasm opcodes used by the slice.
constexpr std::uint8_t kI32Const = 0x41;
constexpr std::uint8_t kI64Const = 0x42;
constexpr std::uint8_t kI64Load = 0x29;
constexpr std::uint8_t kI64Store = 0x37;
constexpr std::uint8_t kI64Add = 0x7C;
constexpr std::uint8_t kI64Mul = 0x7E;
constexpr std::uint8_t kI64Sub = 0x7D;
constexpr std::uint8_t kI64And = 0x83;
constexpr std::uint8_t kI64Or = 0x84;
constexpr std::uint8_t kI64Xor = 0x85;
constexpr std::uint8_t kLocalGet = 0x20;
constexpr std::uint8_t kLocalSet = 0x21;
constexpr std::uint8_t kReturn = 0x0F;
constexpr std::uint8_t kAlign8 = 0x03;
constexpr std::uint8_t kIfVoid = 0x04;
constexpr std::uint8_t kElse = 0x05;
constexpr std::uint8_t kBlockEnd = 0x0B;
// Flags-chain opcodes (parity, shifts, packing into the flags cell).
constexpr std::uint8_t kI32And = 0x71;
constexpr std::uint8_t kI32Or = 0x72;
constexpr std::uint8_t kI32Xor = 0x73;
constexpr std::uint8_t kI32Shl = 0x74;
constexpr std::uint8_t kI32Add = 0x6A;
constexpr std::uint8_t kI32ShrU = 0x76;
constexpr std::uint8_t kI64Eqz = 0x50;
constexpr std::uint8_t kI64LtU = 0x54;
constexpr std::uint8_t kI64ShrU = 0x88;
constexpr std::uint8_t kI64ShlOp = 0x86;
constexpr std::uint8_t kI64RemU = 0x81;
constexpr std::uint8_t kI32Wrap = 0xA7;
constexpr std::uint8_t kI32Store = 0x36;
constexpr std::uint8_t kI32Store8 = 0x3A;
constexpr std::uint8_t kI32Store16 = 0x3B;
constexpr std::uint8_t kI32Load = 0x28;
constexpr std::uint8_t kI32Load8U = 0x2D;
constexpr std::uint8_t kI32Load16U = 0x2F;
constexpr std::uint8_t kI32Eqz = 0x45;
constexpr std::uint8_t kI32LtU = 0x49;
constexpr std::uint8_t kI64ExtendI32U = 0xAD;
constexpr std::uint8_t kSelect = 0x1B;
constexpr std::uint8_t kAlign4 = 0x02;
constexpr std::uint8_t kAlign2 = 0x02;
constexpr std::uint8_t kAlign1 = 0x01;
constexpr std::uint8_t kAlign0 = 0x00;

constexpr unsigned kNumGprs = 16;   ///< One wasm local per guest GPR.
constexpr std::uint32_t kTmpA = 16; ///< i64 scratch: original dst.
constexpr std::uint32_t kTmpB = 17; ///< i64 scratch: source operand.
/// i64 scratch: carry-in (0/1) for adc/sbb flag chains; 0 for plain add/sub.
constexpr std::uint32_t kTmpCi = 18;
constexpr std::uint32_t kTmpC = 19; ///< i64 scratch: cmp/test result.
/// i64 scratch: effective address under page-table walk (mem operands).
constexpr std::uint32_t kTmpV = 20;
/// i64 scratch: page-table frame pointer while walking.
constexpr std::uint32_t kTmpT = 21;
/// i64 scratch: page-table entry / walk address.
constexpr std::uint32_t kTmpE = 22;
/// i64 scratch: translated guest-physical address.
constexpr std::uint32_t kTmpPd = 23;
constexpr std::uint32_t kTmpP = 25;              ///< i32 scratch: parity chain.
constexpr std::uint32_t kFlagsSlot = 256;        ///< i32 flags cell behind the GPRs.
constexpr std::uint32_t kCr3Slot = 264;          ///< i64 cr3 cell (walk root).
constexpr std::uint32_t kCr0Slot = 272;          ///< i64 cr0 cell (WP bit guard).
constexpr std::uint32_t kCplSlot = 280;          ///< u8 cpl cell (0 = kernel).
constexpr std::uint32_t kDoneSlot = 288;         ///< i32 instructions done at handoff.
constexpr std::uint32_t kHandedSlot = 292;       ///< i32 handoff flag (1 = handed back).
constexpr std::uint32_t kStoreCountSlot = 296;   ///< u32 guest stores pending invalidation.
constexpr std::uint32_t kHandoffDirtySlot = 300; ///< u32 dirty_bits written back at handoff.
constexpr std::uint32_t kStoreLogSlot = 304;     ///< 8 store paddrs for note_store.
constexpr std::uint32_t kMirrorBytes = 372;      ///< Total GPR mirror size.

/// Local index of a guest GPR, or nothing when outside the 16-GPR map.
inline std::optional<std::uint32_t> local_of(arch::x86_64::Reg reg) {
    const auto value = static_cast<std::uint8_t>(reg);
    if (value >= kNumGprs) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

/// @brief True unless @p op is an un-REXed h-register byte operand.
/// ah/ch/dh/bh live in byte 1 of the parent slot, off the mirror layout.
/// @param[in] insn Decoded instruction carrying the REX-present fact.
/// @param[in] op Operand to vet.
/// @return True when the operand maps onto a mirror local.
inline bool mirror_ok(const arch::x86_64::DecodedInsn& insn, const Operand& op) {
    if (op.size != 1) {
        return true;
    }
    const auto id = static_cast<unsigned>(op.reg);
    return insn.rex_present || id < 4U || id > 7U;
}

/// @brief True when the operand acts as a GPR slot.
/// Implicit al/cl accumulator forms are included.
/// @param[in] op Operand to test.
/// @return True for kReg and kImplicit operands.
inline bool reglike(const Operand& op) {
    return op.kind == Operand::Kind::kReg || op.kind == Operand::Kind::kImplicit;
}

/// @brief Width mask of an operand size 1..8.
/// Byte yields 0xFF, qword all ones; sizes 1..8 only, callers guard widths.
/// @param[in] size Operand size in bytes.
/// @return Mask covering the low @p size bytes.
inline std::int64_t width_mask_of(unsigned size) {
    switch (size) {
    case 1:
        return 0xFF;
    case 2:
        return 0xFFFF;
    case 4:
        return static_cast<std::int64_t>(UINT64_C(0xFFFFFFFF));
    default:
        return ~INT64_C(0);
    }
}

/// @brief Top sign/carry bit index of an operand size.
/// Sizes 1/2/4/8 map to 7/15/31/63.
/// @param[in] size Operand size in bytes.
/// @return Index of the most significant bit of @p size bytes.
inline unsigned top_bit_of(unsigned size) {
    return (8U * size) - 1U;
}

/// @brief Emits i32.const @p value.
inline void emit_i32(std::vector<std::uint8_t>& body, std::int32_t value) {
    body.push_back(kI32Const);
    append_sleb(body, value);
}

/// @brief Emits i64.const @p value.
inline void emit_i64(std::vector<std::uint8_t>& body, std::int64_t value) {
    body.push_back(kI64Const);
    append_sleb(body, value);
}

/// @brief Emits i64.store at the static offset @p base (align 8).
inline void emit_store(std::vector<std::uint8_t>& body, std::uint32_t base) {
    body.push_back(kI64Store);
    body.push_back(kAlign8);
    append_uleb(body, base);
}

/// @brief Emits i64.load at the static offset @p base (align 8).
inline void emit_load(std::vector<std::uint8_t>& body, std::uint32_t base) {
    body.push_back(kI64Load);
    body.push_back(kAlign8);
    append_uleb(body, base);
}

/// @brief Emits local.get/local.set of local @p idx.
inline void emit_local(std::vector<std::uint8_t>& body, std::uint8_t opcode, std::uint32_t idx) {
    body.push_back(opcode);
    append_uleb(body, idx);
}

/// @brief Writes a width-masked result local into a GPR slot.
///
/// x86 write semantics: 8/16-bit writes merge into the slot's high bits,
/// 32-bit writes zero-extend, 64-bit writes replace. @p res must already be
/// masked to the operand width.
inline void emit_dst_write(std::vector<std::uint8_t>& body, std::uint32_t dst, std::uint32_t res,
                           unsigned size) {
    if (size == 8) {
        emit_local(body, kLocalGet, res);
        emit_local(body, kLocalSet, dst);
        return;
    }
    if (size == 4) {
        emit_local(body, kLocalGet, res);
        emit_i64(body, width_mask_of(4));
        body.push_back(kI64And);
        emit_local(body, kLocalSet, dst);
        return;
    }
    emit_local(body, kLocalGet, dst);
    // NOLINTNEXTLINE(bugprone-signed-bitwise) -- mask complement, not arithmetic
    emit_i64(body, ~width_mask_of(size));
    body.push_back(kI64And);
    emit_local(body, kLocalGet, res);
    body.push_back(kI64Or);
    emit_local(body, kLocalSet, dst);
}

/**
 * @brief Emits the flag chain of one ALU op into the flags cell. Add/sub
 * chains read kTmpCi as carry/borrow-in (callers set 0 without one); the
 * dst result of adc/sbb must already fold it in.
 *
 * Mirrors the interpreter's alu_add/alu_sub/alu_logic (set_zsp included):
 * t_a/t_b hold the masked operands and dst holds the result. The cell
 * layout is cf|zf<<1|sf<<2|of<<3|pf<<4|af<<5, stored at kFlagsSlot.
 * @param[in,out] body Body bytes appended to.
 * @param[in] dst Destination local (holds the result).
 * @param[in] kind 0 = add, 1 = sub, 2 = logic.
 * @param[in] top_bit Top bit index of the width (7, 15, 31 or 63).
 * @param[in] base Static memory offset of the GPR mirror.
 */
void emit_flags(std::vector<std::uint8_t>& body, std::uint32_t dst, unsigned kind, unsigned top_bit,
                std::uint32_t base);

/**
 * @brief Lowers one instruction with a memory operand into wasm body bytes.
 *
 * Supports mov reg,[m] / mov [m],reg / mov [m],imm / cmp/test reg,[m] /
 * add/sub/adc/sbb/and/or/xor reg,[m] / add/sub/adc/sbb/and/or/xor/cmp/test
 * [m],r (lock RMW included) in 8..64-bit width.
 * The access walks the guest page tables inline; any failure returns the
 * step sentinel so the caller takes one interpreter step.
 * @param[in] insn Decoded instruction.
 * @param[in] rip Guest rip of the instruction.
 * @param[in,out] body Body bytes appended to.
 * @param[in] base Static memory offset of the GPR mirror.
 * @param[in] ram_limit Exclusive guest-physical RAM bound in bytes.
 * @return True when lowered, false when the form is unsupported.
 */
bool lower_mem_insn(const arch::x86_64::DecodedInsn& insn, std::uint64_t rip,
                    std::vector<std::uint8_t>& body, std::uint32_t base, std::uint32_t ram_limit,
                    unsigned done, std::uint32_t dirty_bits);

/**
 * @brief Emits a handoff exit: mark the mirror flag, record how many
 *        instructions actually completed, and return @p rip.
 *
 * Used by every "give the rest back to the interpreter" path: ret
 * terminators and mid-block page-walk refusals. The JS runner reads the
 * two mirror cells, commits what ran, retires @p done instructions, and
 * takes one interpreter step from @p rip.
 *
 * @param[in,out] body Wasm body bytes appended to.
 * @param[in] base Static memory offset of the GPR mirror.
 * @param[in] done Instructions of this block that completed.
 * @param[in] rip Guest rip the interpreter should resume from.
 */
void emit_handoff(std::vector<std::uint8_t>& body, std::uint32_t base, unsigned done,
                  std::uint64_t rip, std::uint32_t dirty_bits);

} // namespace micro_forge::base::detail
