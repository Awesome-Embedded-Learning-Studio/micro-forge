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

#include "micro_forge/arch/x86_64/decoder.hpp"

namespace micro_forge::arch::x86_64 {

extern "C" std::uint64_t mf_jit_load(void* cpu, std::uint64_t vaddr, unsigned width);
extern "C" void mf_jit_store(void* cpu, std::uint64_t vaddr, unsigned width, std::uint64_t value);
extern "C" std::uint64_t mf_jit_add(void* cpu, std::uint64_t slot, std::uint64_t imm);
extern "C" void mf_jit_add_mem(void* cpu, std::uint64_t vaddr, std::uint64_t value);

std::optional<Cpu::JitBlock> Cpu::compile_block(uint64_t rip) {
    constexpr std::size_t max_insns = 64;
    // Pass 1 lowers everything first so the page budget is known before
    // any byte is emitted: a shared page must not run out mid-block.
    std::vector<base::TranslatedInsn> items;
    std::size_t budget = 1; // trailing ret for unterminated blocks
    uint64_t covered = 0;
    unsigned count = 0;
    uint64_t pc = rip;
    const uint64_t saved_rip = rip_;
    // fetch() can latch exception_injected_ when a translation fails; the
    // compile pass must not leak that into the interpreter's fault path.
    const bool saved_exception = exception_injected_;
    const auto fail_compile =
        [&]() -> std::optional<JitBlock> { // NOLINT(readability-redundant-lambda-parameter-list)
        rip_ = saved_rip;
        exception_injected_ = saved_exception;
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
        if (decoded->mnemonic == Mnemonic::kRet) {
            --budget; // a terminated block needs no trailing ret
            break;
        }
    }
    rip_ = saved_rip;
    exception_injected_ = saved_exception;
    if (count == 0) {
        return std::nullopt;
    }
    // Pass 2: keep filling the current page while the block fits, else
    // start a fresh one; blocks average ~1.3 KiB so a 4 KiB page holds a
    // few. The unseal/seal round trip is per compile, not per instruction.
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
            std::uint64_t helper = 0;
            if (lowered.helper == base::JitHelper::kAdd) {
                helper = reinterpret_cast<std::uint64_t>(&mf_jit_add);
            } else if (lowered.helper == base::JitHelper::kAddMem) {
                helper = reinterpret_cast<std::uint64_t>(&mf_jit_add_mem);
            } else if (lowered.helper == base::JitHelper::kStore) {
                helper = reinterpret_cast<std::uint64_t>(&mf_jit_store);
            } else {
                helper = reinterpret_cast<std::uint64_t>(&mf_jit_load);
            }
            // A half-patched template must never become a block: give up
            // on the whole compile; the interpreter takes this rip.
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

extern "C" std::uint64_t mf_jit_add(void* cpu, std::uint64_t slot, std::uint64_t imm) {
    return static_cast<Cpu*>(cpu)->jit_add(slot, imm);
}

extern "C" void mf_jit_add_mem(void* cpu, std::uint64_t vaddr, std::uint64_t value) {
    static_cast<Cpu*>(cpu)->jit_add_mem(vaddr, value);
}

} // namespace

uint64_t Cpu::fetch_paddr(uint64_t vaddr) {
    const auto translated = translate_access(vaddr, false, true, vaddr);
    return translated.ok ? translated.paddr : 0;
}

uint64_t Cpu::jit_add(uint64_t slot, uint64_t imm) {
    // The template loads the immediate with `mov edx, imm32`, which zeroes
    // the upper half; ADD r64,imm32 sign-extends, so restore that here.
    imm = static_cast<uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(imm)));
    const auto index = static_cast<unsigned>(slot / 8U);
    const uint64_t a = regs_[index];
    const uint64_t result = a + imm;
    regs_[index] = result;
    // Flags follow the interpreter's 64-bit add semantics.
    cf_ = result < a;
    zf_ = result == 0;
    sf_ = (result >> 63U) != 0;
    of_ = (((a ^ result) & (imm ^ result)) >> 63U) != 0;
    pf_ = (std::popcount(static_cast<std::uint8_t>(result)) % 2) == 0;
    af_ = ((a ^ imm ^ result) & 0x10U) != 0;
    return result;
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
    // Store errors are latched by the bus; the template has no way to
    // react to them, so the results are deliberately discarded here.
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
    // Each block owns a 4 KiB page; 32,768 blocks cost ~128 MiB of the
    // 2 GiB MAP_32BIT window. Raising the cap from 4,096 lifted the
    // kernel-boot coverage from 25.9% to 33% (codex measurement).
    constexpr std::size_t max_blocks = 32768;
    if ((!jit_requested_ && !env_jit) || jit_blocks_.contains(rip) || jit_failed_.contains(rip) ||
        jit_blocks_.size() >= max_blocks) {
        return;
    }
    // Only compile inside the declared code range. A flat kernel has no
    // segment table, but the loader knows the file extent, so data and
    // bss never become blocks.
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
    // Empty table is the common case (JIT disabled): skip the hash lookup.
    if (jit_blocks_.empty()) {
        return false;
    }
    const auto it = jit_blocks_.find(rip_);
    if (it == jit_blocks_.end()) {
        return false;
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe) -- single-threaded CPU
    static const bool trace = std::getenv("MICRO_FORGE_JIT_TRACE") != nullptr;
    if (trace) {
        std::println(stderr, "jit: run rip=0x{:x} insns={}", rip_, it->second.insn_count);
    }
    it->second.page->invoke(it->second.entry, regs_.data());
    rip_ += it->second.length;
    jit_insn_count_ += it->second.insn_count;
    retire(it->second.insn_count);
    return true;
}

} // namespace micro_forge::arch::x86_64
