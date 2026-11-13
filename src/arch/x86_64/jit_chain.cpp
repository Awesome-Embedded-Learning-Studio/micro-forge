/**
 * @file jit_chain.cpp
 * @brief Block-exit tails and lazy direct-jmp chaining (stage 2).
 * @version 0.1.0
 *
 * Every terminated block carries an exit tail over its template's ret. A
 * block eaten by a chained hop never passes through run_jit_block, so
 * only its own tail can retire its instruction count. jmp-imm blocks get
 * the full rewritable chain exit (try_link patches the jnz rel32);
 * jcc/ret/call blocks get a plain add-only accounting tail. The tails
 * address the retire counter as [r11+8]: it lives adjacent to the chain
 * budget in the jit_chain_ array.
 */

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <print>

#include "micro_forge/arch/x86_64/cpu.hpp"

namespace micro_forge::arch::x86_64 {

std::optional<std::size_t> Cpu::append_link_tail(base::JitPage& page, uint64_t target,
                                                 uint64_t guard, unsigned cnt) {
    const auto slot = reinterpret_cast<std::uint64_t>(&jit_chain_[0]);
    const auto insns = static_cast<std::uint32_t>(cnt);
    if (target != 0) {
        // Rewritable chain exit, laid over the template's trailing ret:
        //   movabs r11, &jit_chain_[0]    ; 10  budget +0, insns +8, due +16
        //   add qword ptr [r11+8], count  ;  8  retire-on-exit accounting
        //   mov rax, [r11+8]              ;  4  insns retired since surfacing
        //   cmp rax, [r11+16]             ;  4  device deadline reached?
        //   jae .ret                      ;  2  due: surface for exact retire
        //   dec qword ptr [r11]           ;  3  chain budget
        //   jnz <rel32, 0 until linked>   ;  6  jump straight to the target
        //   mov qword ptr [r11], 64       ;  7  re-arm and surface
        //   .ret: ret                     ;  1
        const std::array<std::uint8_t, 44> pad{};
        if (!page.emit_multi(pad, {}).has_value()) {
            return std::nullopt;
        }
        const std::size_t seq_at = page.used() - 45; // the template's ret
        std::array<std::uint8_t, 45> seq{};
        seq[0] = 0x49;
        seq[1] = 0xBB;
        std::memcpy(&seq[2], &slot, 8);
        seq[10] = 0x49;
        seq[11] = 0x81;
        seq[12] = 0x43;
        seq[13] = 0x08;
        std::memcpy(&seq[14], &insns, 4);
        seq[18] = 0x49;
        seq[19] = 0x8B; // mov rax, [r11+8]
        seq[20] = 0x43;
        seq[21] = 0x08;
        seq[22] = 0x49; // cmp rax, [r11+16]
        seq[23] = 0x3B;
        seq[24] = 0x43;
        seq[25] = 0x10;
        seq[26] = 0x73; // jae .ret (+16 -> seq[44])
        seq[27] = 0x10;
        seq[28] = 0x49;
        seq[29] = 0xFF; // dec qword ptr [r11]
        seq[30] = 0x0B;
        seq[31] = 0x0F;
        seq[32] = 0x85; // rel32 stays zero: jnz $+6 falls through to re-arm
        seq[37] = 0x49;
        seq[38] = 0xC7;
        seq[39] = 0x03;
        seq[40] = 64; // chain length before surfacing for retirement
        seq[44] = 0xC3;
        if (!page.write_bytes(seq_at, seq)) {
            return std::nullopt;
        }
        return seq_at;
    }
    if (guard != 0) {
        // Guard stub for a jcc exit (over the template's ret). The helper
        // has just written the taken/fallthrough rip into jit_chain_[3];
        // when it took the fallthrough (== guard rip) the stub chains the
        // fallthrough block, otherwise it surfaces to run_jit_block:
        //   movabs r11, &jit_chain_[0]    ; 10
        //   add qword ptr [r11+8], count  ;  8  retire-on-exit accounting
        //   cmp qword ptr [r11+24], guard ;  8  helper's exit snapshot
        //   jne .ret                      ;  2  taken: surface to C++
        //   mov rax, [r11+8]              ;  4  insns retired since surfacing
        //   cmp rax, [r11+16]             ;  4  device deadline reached?
        //   jae .ret                      ;  2  due: surface for exact retire
        //   dec qword ptr [r11]           ;  3  chain budget
        //   jnz <rel32, 0 until linked>   ;  6  jump straight to the target
        //   mov qword ptr [r11], 64       ;  7  re-arm and surface
        //   .ret: ret                     ;  1
        const std::array<std::uint8_t, 54> pad{};
        if (!page.emit_multi(pad, {}).has_value()) {
            return std::nullopt;
        }
        const std::size_t seq_at = page.used() - 55; // the template's ret
        std::array<std::uint8_t, 55> seq{};
        seq[0] = 0x49;
        seq[1] = 0xBB;
        std::memcpy(&seq[2], &slot, 8);
        seq[10] = 0x49; // add qword ptr [r11+8], count (both paths retire)
        seq[11] = 0x81;
        seq[12] = 0x43;
        seq[13] = 0x08;
        std::memcpy(&seq[14], &insns, 4);
        seq[18] = 0x49; // cmp qword ptr [r11+24], imm32 (guard rip)
        seq[19] = 0x81;
        seq[20] = 0x7B;
        seq[21] = 0x18;
        const auto g32 = static_cast<std::uint32_t>(static_cast<std::int64_t>(guard));
        std::memcpy(&seq[22], &g32, 4);
        seq[26] = 0x75; // jne .ret (+26 -> seq[54])
        seq[27] = 26;
        seq[28] = 0x49; // mov rax, [r11+8]
        seq[29] = 0x8B;
        seq[30] = 0x43;
        seq[31] = 0x08;
        seq[32] = 0x49; // cmp rax, [r11+16]
        seq[33] = 0x3B;
        seq[34] = 0x43;
        seq[35] = 0x10;
        seq[36] = 0x73; // jae .ret (+16 -> seq[54])
        seq[37] = 16;
        seq[38] = 0x49; // dec qword ptr [r11]
        seq[39] = 0xFF;
        seq[40] = 0x0B;
        seq[41] = 0x0F; // jnz rel32: zero until try_link wires it
        seq[42] = 0x85;
        seq[47] = 0x49; // mov qword ptr [r11], 64
        seq[48] = 0xC7;
        seq[49] = 0x03;
        seq[50] = 64; // chain length before surfacing for retirement
        seq[54] = 0xC3;
        if (!page.write_bytes(seq_at, seq)) {
            return std::nullopt;
        }
        return seq_at;
    }
    return std::nullopt; // neither form requested
}

bool Cpu::append_plain_tail(base::JitPage& page, unsigned cnt) {
    // Accounting-only tail for ret/call exits: they always return to
    // run_jit_block, so no chain machinery is needed -- but a hop can eat
    // this block, and only this tail can then retire its count.
    //   movabs r11, &jit_chain_[0]    ; 10
    //   add qword ptr [r11+8], count  ;  8
    //   ret                           ;  1
    const auto slot = reinterpret_cast<std::uint64_t>(&jit_chain_[0]);
    const auto insns = static_cast<std::uint32_t>(cnt);
    const std::array<std::uint8_t, 18> pad{};
    if (!page.emit_multi(pad, {}).has_value()) {
        return false;
    }
    const std::size_t seq_at = page.used() - 19; // the template's ret
    std::array<std::uint8_t, 19> seq{};
    seq[0] = 0x49;
    seq[1] = 0xBB;
    std::memcpy(&seq[2], &slot, 8);
    seq[10] = 0x49; // add qword ptr [r11+8], count
    seq[11] = 0x81;
    seq[12] = 0x43;
    seq[13] = 0x08;
    std::memcpy(&seq[14], &insns, 4);
    seq[18] = 0xC3;
    return page.write_bytes(seq_at, seq);
}

void Cpu::try_link(JitBlock& blk) {
    static const bool nochain =                            // A/B experiment
        std::getenv("MICRO_FORGE_JIT_NOCHAIN") != nullptr; // NOLINT(concurrency-mt-unsafe)
    if (nochain) {
        return;
    }
    const auto patch = blk.patch_at;
    if (!patch.has_value() || blk.linked) {
        return; // only jmp/jcc blocks carry a rewritable exit
    }
    // jcc guard stubs chain the fallthrough exit only when the helper
    // actually took it; a taken jcc keeps surfacing to run_jit_block.
    if (blk.guard_rip != 0 && jit_next_rip_ != blk.guard_rip) {
        return;
    }
    const uint64_t target_rip = blk.const_target != 0 ? blk.const_target : blk.guard_rip;
    const auto target = jit_blocks_.find(target_rip);
    if (target == jit_blocks_.end()) {
        return; // not compiled (yet): the plain ret path stays correct
    }
    if (!target->second.sets_next) {
        return; // unterminated targets have no retirement tail and would
                // even re-execute under the hop; terminated targets all
                // carry accounting tails (45B/61B/19B)
    }
    // rel32 of the jnz: measured from the end of the jump instruction
    // (31+6 in the 45B exit; 41+6 in the 55B guard stub).
    const bool guard_stub = blk.guard_rip != 0;
    const std::size_t rel_at = *patch + (guard_stub ? 43 : 33);
    const auto* from = blk.page->base() + *patch + (guard_stub ? 47 : 37);
    const auto* to = target->second.page->base() + target->second.entry;
    const auto rel = static_cast<std::int64_t>(to - from);
    if (rel < INT32_MIN || rel > INT32_MAX) {
        return; // pages live in the 2 GiB MAP_32BIT window, but be safe
    }
    const auto rel32 = static_cast<std::uint64_t>(rel);
    if (!blk.page->unseal()) {
        return;
    }
    const bool ok = blk.page->patch(rel_at, 4, rel32);
    (void)blk.page->seal(); // back to RX either way; failure keeps the ret
    blk.linked = ok;
    if (ok &&
        std::getenv("MICRO_FORGE_JIT_LINKSTATS") != nullptr) { // NOLINT(concurrency-mt-unsafe)
        static unsigned links = 0;                             // NOLINT(concurrency-mt-unsafe)
        std::println(stderr, "jit: link #{} rip_target=0x{:x}", ++links, target_rip);
    }
}

} // namespace micro_forge::arch::x86_64
