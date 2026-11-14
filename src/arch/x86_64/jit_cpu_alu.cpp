/**
 * @file jit_cpu_alu.cpp
 * @brief JIT-side ALU and push/pop helpers reusing the interpreter ALU.
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/cpu.hpp"

#include "internal.hpp"
#include "micro_forge/arch/x86_64/mul_wide.hpp"

#include <cstdint>

namespace micro_forge::arch::x86_64 {

using width_util::merge_width;

// Share the interpreter's ALU, including its borrow and AF semantics.
// Both ALUs mask the inputs/result to width, so dword writes clear high bits.
void Cpu::jit_cmp(uint64_t slot, uint64_t value, unsigned width) {
    (void)alu_sub(regs_[slot / 8U], value, width, false);
}

void Cpu::jit_test(uint64_t slot, uint64_t value, unsigned width) {
    (void)alu_logic(regs_[slot / 8U] & value, width);
}

void Cpu::jit_sub(uint64_t slot, uint64_t value, unsigned width) {
    auto& dst = regs_[slot / 8U];
    dst = merge_width(dst, alu_sub(dst, value, width, false), width);
}

void Cpu::jit_and(uint64_t slot, uint64_t value, unsigned width) {
    auto& dst = regs_[slot / 8U];
    dst = merge_width(dst, alu_logic(dst & value, width), width);
}

void Cpu::jit_push_val(uint64_t value) {
    (void)push_value(value, 8);
}

void Cpu::jit_push_imm(uint64_t value) {
    (void)push_value(value, 8);
}

void Cpu::jit_load_xmm(uint64_t vaddr, uint64_t slot) {
    const auto idx = static_cast<std::size_t>(slot / 16U);
    if (idx >= xmm_.size()) {
        return;
    }
    const uint64_t low = jit_load(vaddr, 8);
    if (!exception_injected_) {
        const uint64_t high = jit_load(vaddr + 8U, 8);
        if (!exception_injected_) {
            xmm_[idx] = {low, high};
        }
    }
}

void Cpu::jit_store_xmm(uint64_t vaddr, uint64_t slot) {
    const auto idx = static_cast<std::size_t>(slot / 16U);
    if (idx >= xmm_.size()) {
        return;
    }
    jit_store(vaddr, 8, xmm_[idx][0]);
    if (!exception_injected_) {
        jit_store(vaddr + 8U, 8, xmm_[idx][1]);
    }
}

void Cpu::jit_pop_reg(uint64_t slot) {
    uint64_t v = 0;
    if (pop_value(8, v)) {
        regs_[slot / 8U] = v;
    }
}

void Cpu::jit_setcc(uint64_t slot, unsigned cc) {
    auto& dst = regs_[slot / 8U];
    const uint64_t val = condition(static_cast<Cc>(cc)) ? 1U : 0U;
    dst = merge_width(dst, val, 1);
}

void Cpu::jit_cmov(uint64_t slot, uint64_t src_val, unsigned cc, unsigned width) {
    if (!condition(static_cast<Cc>(cc))) {
        return; // condition false: destination keeps its old value
    }
    auto& dst = regs_[slot / 8U];
    dst = merge_width(dst, src_val & width_util::width_mask(width), width);
}

void Cpu::jit_or(uint64_t slot, uint64_t value, unsigned width) {
    auto& dst = regs_[slot / 8U];
    dst = merge_width(dst, alu_logic(dst | value, width), width);
}

void Cpu::jit_xor(uint64_t slot, uint64_t value, unsigned width) {
    auto& dst = regs_[slot / 8U];
    dst = merge_width(dst, alu_logic(dst ^ value, width), width);
}

// Two-operand imul mirrors execute_imul23 exactly: sign-extend both sides,
// write the truncated product back, and set CF=OF only when the product does
// not fit its half. The wide-multiply primitives come from mul_wide.hpp, the
// same ones the interpreter uses, so the flags are bit-identical.
void Cpu::jit_imul(uint64_t slot, uint64_t value, unsigned width) {
    const auto lhs = static_cast<int64_t>(width_util::sign_extend(regs_[slot / 8U], width));
    const auto rhs = static_cast<int64_t>(width_util::sign_extend(value, width));
    const uint64_t low = static_cast<uint64_t>(lhs) * static_cast<uint64_t>(rhs);
    const uint64_t masked = low & width_util::width_mask(width);
    const bool fits =
        width == 8U ? imul_high(lhs, rhs) == sign_fill_of(low)
                    : static_cast<int64_t>(width_util::sign_extend(masked, width)) == lhs * rhs;
    regs_[slot / 8U] = masked;
    cf_ = of_ = !fits;
}

} // namespace micro_forge::arch::x86_64
