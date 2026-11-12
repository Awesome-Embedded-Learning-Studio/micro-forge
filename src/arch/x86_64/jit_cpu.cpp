/**
 * @file jit_cpu.cpp
 * @brief The CPU's copy-and-patch block compiler (native backend).
 * @version 0.1.0
 *
 * Compiled blocks live in Cpu::jit_blocks_ as JitBlock entries: an
 * executable page plus the guest bytes/instructions it covers. The
 * interpreter slow path compiles a block on first visit; later visits
 * to the same rip run the block with retire() applied per instruction.
 */

#include <array>
#include <bit>
#include <cstdlib>
#include <print>

#include "micro_forge/arch/x86_64/cpu.hpp"

#include "internal.hpp"
#include "jit_thunks.hpp"
#include "micro_forge/arch/x86_64/decoder.hpp"
#include "micro_forge/arch/x86_64/mul_wide.hpp"

namespace micro_forge::arch::x86_64 {

std::optional<Cpu::JitBlock> Cpu::compile_block(uint64_t rip) {
    constexpr std::size_t max_insns = 64;
    // Pass 1 lowers everything first: a page must not run dry.
    std::vector<base::TranslatedInsn> items;
    std::size_t budget = 1; // trailing ret for unterminated blocks
    uint64_t covered = 0;
    unsigned count = 0;
    bool terminated = false;
    uint64_t pc = rip;
    const uint64_t saved_rip = rip_;
    // fetch() can latch exception_injected_; keep it out of faults.
    const bool saved_exception = exception_injected_;
    const auto fail_compile =
        [&]() -> std::optional<JitBlock> { // NOLINT(readability-redundant-lambda-parameter-list)
        rip_ = saved_rip;
        exception_injected_ = saved_exception;
        // A half-emitted page must go back to read-execute: live blocks.
        if (jit_page_) {
            (void)jit_page_->seal(); // idempotent: false when already sealed
        }
        jit_failed_.insert(rip);
        return std::nullopt;
    };
    while (count < max_insns) {
        rip_ = pc;
        unsigned got = 0;
        if (!fetch(got)) {
            break;
        }
        const auto decoded = decode_one(std::span<const std::uint8_t>(fetch_buf_.data(), got));
        if (!decoded) {
            break;
        }
        auto lowered = base::translate_insn(*decoded, pc);
        if (!lowered) {
            break; // unsupported: end the block here, interpreter takes over
        }
        // Runtime structural assertions, before emit_multi fills any hole:
        // call_offset must follow 49 BB; cpu_offset must follow 48 BF.
        // Also reject patches that overwrite opcodes or overlap relocations.
        if (!lowered->has_valid_layout()) {
            std::println(stderr, "jit: invalid template at rip=0x{:x} (block=0x{:x})", pc, rip);
            return fail_compile();
        }
        budget += lowered->code.size();
        items.push_back(std::move(*lowered));
        covered += decoded->length;
        ++count;
        pc += decoded->length;
        // Templates ending in their own host ret close the block: anything
        // past them would be emitted but never executed.
        switch (decoded->mnemonic) {
        case Mnemonic::kRet:
        case Mnemonic::kCall:
        case Mnemonic::kJmp:
        case Mnemonic::kJcc:
            --budget;
            terminated = true;
            break;
        default:
            break;
        }
        if (terminated) {
            break;
        }
    }
    rip_ = saved_rip;
    exception_injected_ = saved_exception;
    if (count == 0) {
        return std::nullopt;
    }
    // Pass 2: fill the current page while the block fits, else start a new
    // one; unseal/seal round trips happen once per compile.
    if (!jit_page_ || jit_page_->available() < budget) {
        auto fresh = base::JitPage::create();
        if (!fresh.has_value()) {
            return fail_compile();
        }
        jit_page_ = std::make_shared<base::JitPage>(std::move(*fresh));
    } else if (!jit_page_->unseal()) {
        return fail_compile();
    }
    auto& page = *jit_page_;
    const std::size_t entry = page.used();
    for (const base::TranslatedInsn& lowered : items) {
        const auto offset = page.emit_multi(lowered.code, lowered.patches);
        if (!offset.has_value()) {
            return fail_compile();
        }
        if (lowered.call_offset.has_value() && lowered.cpu_offset.has_value()) {
            const auto cpu_arg = reinterpret_cast<std::uint64_t>(this);
            const std::uint64_t helper = mf_jit_helper_address(lowered.helper);
            if (helper == 0) {
                return fail_compile();
            }
            // A half-patched template must never become a block.
            if (!page.patch(*offset + *lowered.cpu_offset, 8, cpu_arg) ||
                !page.patch(*offset + *lowered.call_offset, 8, helper)) {
                return fail_compile();
            }
        }
    }
    if (budget > 1) { // unterminated block: append the shared ret
        const std::array<std::uint8_t, 1> ret = {0xC3};
        if (!page.emit_multi(ret, {}).has_value()) {
            return fail_compile();
        }
    }
    if (!page.seal()) {
        return std::nullopt;
    }
    return JitBlock{
        .page = jit_page_,
        .entry = entry,
        .length = covered,
        .insn_count = count,
    };
}

namespace {

/// C ABI bridge the JIT template calls (rdi=Cpu*, rsi=vaddr, edx=width).
extern "C" std::uint64_t mf_jit_load(void* cpu, std::uint64_t vaddr, unsigned width) {
    return static_cast<Cpu*>(cpu)->jit_load(vaddr, width);
}

extern "C" void mf_jit_store(void* cpu, std::uint64_t vaddr, unsigned width, std::uint64_t value) {
    static_cast<Cpu*>(cpu)->jit_store(vaddr, width, value);
}

extern "C" std::uint64_t mf_jit_add(void* cpu, std::uint64_t slot, std::uint64_t imm,
                                    unsigned width) {
    return static_cast<Cpu*>(cpu)->jit_add(slot, imm, width);
}

extern "C" void mf_jit_add_mem(void* cpu, std::uint64_t vaddr, std::uint64_t value) {
    static_cast<Cpu*>(cpu)->jit_add_mem(vaddr, value);
}

extern "C" void mf_jit_cmp(void* cpu, uint64_t slot, uint64_t value, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_cmp(slot, value, width);
}

extern "C" void mf_jit_test(void* cpu, uint64_t slot, uint64_t value, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_test(slot, value, width);
}

extern "C" void mf_jit_sub(void* cpu, uint64_t slot, uint64_t value, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_sub(slot, value, width);
}

extern "C" void mf_jit_and(void* cpu, uint64_t slot, uint64_t value, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_and(slot, value, width);
}

extern "C" void mf_jit_cmov(void* cpu, uint64_t slot, uint64_t src_val, unsigned cc,
                            unsigned width) {
    static_cast<Cpu*>(cpu)->jit_cmov(slot, src_val, cc, width);
}

extern "C" void mf_jit_setcc(void* cpu, uint64_t slot, unsigned cc) {
    static_cast<Cpu*>(cpu)->jit_setcc(slot, cc);
}

extern "C" void mf_jit_push_val(void* cpu, uint64_t value) {
    static_cast<Cpu*>(cpu)->jit_push_val(value);
}

extern "C" void mf_jit_push_imm(void* cpu, uint64_t value) {
    static_cast<Cpu*>(cpu)->jit_push_imm(value);
}

extern "C" void mf_jit_load_xmm(void* cpu, uint64_t vaddr, uint64_t slot) {
    static_cast<Cpu*>(cpu)->jit_load_xmm(vaddr, slot);
}

extern "C" void mf_jit_store_xmm(void* cpu, uint64_t vaddr, uint64_t slot) {
    static_cast<Cpu*>(cpu)->jit_store_xmm(vaddr, slot);
}

extern "C" void mf_jit_pop_reg(void* cpu, uint64_t slot) {
    static_cast<Cpu*>(cpu)->jit_pop_reg(slot);
}

extern "C" void mf_jit_or(void* cpu, uint64_t slot, uint64_t value, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_or(slot, value, width);
}

extern "C" void mf_jit_xor(void* cpu, uint64_t slot, uint64_t value, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_xor(slot, value, width);
}

extern "C" void mf_jit_imul(void* cpu, uint64_t slot, uint64_t value, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_imul(slot, value, width);
}

extern "C" void mf_jit_cmp_mem(void* cpu, uint64_t vaddr, uint64_t slot, unsigned width) {
    static_cast<Cpu*>(cpu)->jit_cmp_mem(vaddr, slot, width);
}

} // namespace

uint64_t Cpu::fetch_paddr(uint64_t vaddr) {
    const auto translated = translate_access(vaddr, false, true, vaddr);
    return translated.ok ? translated.paddr : 0;
}

namespace {

// 8/16-bit writes keep the destination's upper bytes; 32-bit writes
// zero-extend, exactly like the interpreter's write_operand.
uint64_t merge_width(uint64_t dst, uint64_t result, unsigned width) {
    const uint64_t mask = width == 1U ? 0xFFULL : 0xFFFFULL;
    return width == 4U || width == 8U ? result : (dst & ~mask) | result;
}

} // namespace

uint64_t Cpu::jit_add(uint64_t slot, uint64_t imm, unsigned width) {
    // `mov edx, imm32` zeroes the upper half; r64 needs sign extension.
    imm = static_cast<uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(imm)));
    const auto index = static_cast<unsigned>(slot / 8U);
    const uint64_t a = width == 4 ? static_cast<uint32_t>(regs_[index]) : regs_[index];
    const uint64_t result = width == 4 ? static_cast<uint32_t>(a + imm) : a + imm;
    regs_[index] = merge_width(regs_[index], result, width);
    // A dword result is zero-extended; the qword path keeps its original arithmetic.
    const unsigned sign = width == 4 ? 31U : 63U;
    cf_ = result < a;
    zf_ = result == 0;
    sf_ = (result >> sign) != 0;
    of_ = (((a ^ result) & (imm ^ result)) >> sign & 1U) != 0;
    pf_ = (std::popcount(static_cast<std::uint8_t>(result)) % 2) == 0;
    af_ = ((a ^ imm ^ result) & 0x10U) != 0;
    return result;
}

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

void Cpu::jit_cmp_mem(uint64_t vaddr, uint64_t slot, unsigned width) {
    const uint64_t value = jit_load(vaddr, width);
    if (!exception_injected_) {
        jit_cmp(slot, value, width);
    }
}

uint64_t Cpu::jit_load(uint64_t vaddr, unsigned width) {
    const auto translated = translate_access(vaddr, false, false, insn_rip_);
    if (!translated.ok) {
        return 0;
    }
    switch (width) {
    case 1:
        return static_cast<uint64_t>(bus_.read_u8(translated.paddr).value_or(0));
    case 2:
        return static_cast<uint64_t>(bus_.read_u16(translated.paddr).value_or(0));
    case 4:
        return static_cast<uint64_t>(bus_.read_u32(translated.paddr).value_or(0));
    default:
        return bus_.read_u64(translated.paddr).value_or(0);
    }
}

void Cpu::jit_add_mem(uint64_t vaddr, uint64_t value) {
    const uint64_t previous = jit_load(vaddr, 8);
    const uint64_t result = alu_add(previous, value, 8, false);
    jit_store(vaddr, 8, result);
}

void Cpu::jit_store(uint64_t vaddr, unsigned width, uint64_t value) {
    const auto translated = translate_access(vaddr, true, false, insn_rip_);
    if (!translated.ok) {
        return;
    }
    // Store errors latch in the bus; the results are discarded here.
    switch (width) {
    case 1: {
        [[maybe_unused]] const auto w =
            bus_.write_u8(translated.paddr, static_cast<std::uint8_t>(value));
        break;
    }
    case 2: {
        [[maybe_unused]] const auto w =
            bus_.write_u16(translated.paddr, static_cast<std::uint16_t>(value));
        break;
    }
    case 4: {
        [[maybe_unused]] const auto w =
            bus_.write_u32(translated.paddr, static_cast<std::uint32_t>(value));
        break;
    }
    default: {
        [[maybe_unused]] const auto w = bus_.write_u64(translated.paddr, value);
        break;
    }
    }
}

void Cpu::maybe_compile_block(uint64_t rip) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe) -- single-threaded CPU
    static const bool env_jit = std::getenv("MICRO_FORGE_JIT") != nullptr;
    // ~128 MiB of the 2 GiB MAP_32BIT window; 4,096 capped boot coverage.
    constexpr std::size_t max_blocks = 32768;
    if ((!jit_requested_ && !env_jit) || jit_blocks_.contains(rip) || jit_failed_.contains(rip) ||
        jit_blocks_.size() >= max_blocks) {
        return;
    }
    // Only compile inside the declared code range: the loader knows the
    // file extent, so data and bss never become blocks.
    if (code_hi_ > code_lo_ && (rip < code_lo_ || rip >= code_hi_)) {
        return;
    }
    if (auto compiled = compile_block(rip)) {
        jit_blocks_.emplace(rip, std::move(*compiled));
    } else {
        jit_failed_.insert(rip);
    }
}

bool Cpu::run_jit_block() {
    // A helper faulted: one interpreter step re-raises; dispatch resumes.
    if (jit_suspend_) {
        jit_suspend_ = false;
        return false;
    }
    // Empty table is the common case (JIT disabled): skip the hash lookup.
    if (jit_blocks_.empty()) {
        return false;
    }
    const auto it = jit_blocks_.find(rip_);
    if (it == jit_blocks_.end()) {
        mf_jit_note_fail_hit(rip_); // diagnostics: which rip falls back
        return false;
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe) -- single-threaded CPU
    static const bool trace = std::getenv("MICRO_FORGE_JIT_TRACE") != nullptr;
    if (trace) {
        std::println(stderr, "jit: run rip=0x{:x} insns={}", rip_, it->second.insn_count);
    }
    it->second.page->invoke(it->second.entry, regs_.data());
    // jit_ret/jit_call record their own exit; others fall through.
    if (jit_next_set_) {
        rip_ = jit_next_rip_;
        jit_next_set_ = false;
    } else {
        rip_ += it->second.length;
    }
    jit_insn_count_ += it->second.insn_count;
    retire(it->second.insn_count);
    return true;
}

} // namespace micro_forge::arch::x86_64
