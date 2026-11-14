//! \file wasm_block_stack.cpp
//! Guest stack operations (push / pop / ret) for the wasm block builder.
//!
//! All three move rsp through the same inline page walk the data
//! instructions use. The walk runs before rsp changes, so a refusal leaves
//! the stack exactly as the interpreter expects to find it.

#include "micro_forge/base/wasm_block_detail.hpp"
#include "micro_forge/base/wasm_block_internal.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace micro_forge::base::detail {

using arch::x86_64::DecodedInsn;
using arch::x86_64::Mnemonic;
using arch::x86_64::Operand;
using arch::x86_64::Reg;

namespace {

/// rsp slot of the register bridge.
std::optional<std::uint32_t> rsp_slot() {
    return local_of(Reg::kRsp);
}

/// Reads [@p addr_reg] of @p size bytes into kTmpC, then moves rsp by @p delta.
void emit_load_frame(std::vector<std::uint8_t>& body, std::uint32_t rsp, unsigned size,
                     std::uint32_t ram_limit, const Handoff& ho, std::int64_t delta) {
    emit_local(body, kLocalGet, rsp);
    emit_local(body, kLocalSet, kTmpV);
    emit_walk(body, size, ram_limit, ho, /*write=*/false);
    emit_guest_load(body, size);
    emit_local(body, kLocalSet, kTmpC);
    emit_local(body, kLocalGet, rsp);
    emit_i64(body, delta);
    body.push_back(delta > 0 ? kI64Add : kI64Sub);
    emit_local(body, kLocalSet, rsp);
}

/// Stores @p value_slot at rsp-8, then moves rsp down by 8.
void emit_store_frame(std::vector<std::uint8_t>& body, std::uint32_t rsp, std::uint32_t value_slot,
                      unsigned size, std::uint32_t ram_limit, const Handoff& ho,
                      std::uint32_t base) {
    emit_local(body, kLocalGet, rsp);
    emit_i64(body, 8);
    body.push_back(kI64Sub);
    emit_local(body, kLocalSet, kTmpV);
    emit_walk(body, size, ram_limit, ho, /*write=*/true);
    emit_local(body, kLocalGet, value_slot);
    emit_local(body, kLocalSet, kTmpC);
    emit_local(body, kLocalGet, kTmpC); // store wants (paddr, value)
    emit_guest_store(body, size);
    emit_store_log(body, base);
    emit_local(body, kLocalGet, rsp);
    emit_i64(body, 8);
    body.push_back(kI64Sub);
    emit_local(body, kLocalSet, rsp);
}

} // namespace

bool lower_stack_insn(const DecodedInsn& insn, std::uint64_t rip, std::vector<std::uint8_t>& body,
                      TermExit& term, std::uint32_t base, std::uint32_t ram_limit, unsigned done,
                      std::uint32_t dirty_so_far) {
    const auto rsp = rsp_slot();
    if (!rsp.has_value()) {
        return false;
    }
    const Handoff ho{.base = base, .done = done, .rip = rip, .dirty_bits = dirty_so_far};
    if (insn.mnemonic == Mnemonic::kPush) {
        if (insn.ops[0].kind != Operand::Kind::kReg &&
            insn.ops[0].kind != Operand::Kind::kImplicit) {
            return false; // push [mem] / push imm8 stay interpreted
        }
        const auto src = local_of(insn.ops[0].reg);
        const unsigned size = insn.ops[0].size;
        if (!src.has_value() || size == 0 || size > 8 || !mirror_ok(insn, insn.ops[0])) {
            return false;
        }
        emit_store_frame(body, *rsp, *src, size, ram_limit, ho, base);
        return true;
    }
    if (insn.mnemonic == Mnemonic::kPop) {
        if (insn.ops[0].kind != Operand::Kind::kReg &&
            insn.ops[0].kind != Operand::Kind::kImplicit) {
            return false;
        }
        const auto dst = local_of(insn.ops[0].reg);
        const unsigned size = insn.ops[0].size;
        if (!dst.has_value() || size == 0 || size > 8 || !mirror_ok(insn, insn.ops[0])) {
            return false;
        }
        emit_load_frame(body, *rsp, size, ram_limit, ho, 8);
        emit_dst_write(body, *dst, kTmpC, size);
        return true;
    }
    if (insn.mnemonic == Mnemonic::kRet) {
        emit_load_frame(body, *rsp, 8, ram_limit, ho, 8);
        term.kind = WasmTerm::kIndirect; // popped return address
        return true;
    }
    return false;
}

} // namespace micro_forge::base::detail
