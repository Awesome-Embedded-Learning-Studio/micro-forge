/**
 * @file sysregs.cpp
 * @brief System-register and system-instruction execution (batch 6a).
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/cpu.hpp"

#include <algorithm>
#include <array>

namespace micro_forge::arch::x86_64 {

uint64_t Cpu::rflags() const {
    uint64_t f = 0x2ULL; // reserved bit 1 reads as one
    f |= cf_ ? kernel::bit::mask_bit(0) : 0U;
    f |= pf_ ? kernel::bit::mask_bit(2) : 0U;
    f |= af_ ? kernel::bit::mask_bit(4) : 0U;
    f |= zf_ ? kernel::bit::mask_bit(6) : 0U;
    f |= sf_ ? kernel::bit::mask_bit(7) : 0U;
    f |= if_ ? kernel::bit::mask_bit(9) : 0U;
    f |= df_ ? kernel::bit::mask_bit(10) : 0U;
    f |= of_ ? kernel::bit::mask_bit(11) : 0U;
    f |= ac_ ? kernel::bit::mask_bit(18) : 0U;
    return f;
}

void Cpu::set_rflags(uint64_t value) {
    cf_ = (value & kernel::bit::mask_bit(0)) != 0;
    pf_ = (value & kernel::bit::mask_bit(2)) != 0;
    af_ = (value & kernel::bit::mask_bit(4)) != 0;
    zf_ = (value & kernel::bit::mask_bit(6)) != 0;
    sf_ = (value & kernel::bit::mask_bit(7)) != 0;
    if_ = (value & kernel::bit::mask_bit(9)) != 0;
    df_ = (value & kernel::bit::mask_bit(10)) != 0;
    ac_ = (value & kernel::bit::mask_bit(18)) != 0;
    of_ = (value & kernel::bit::mask_bit(11)) != 0;
}

namespace {

// MSR whitelist: exactly what the Cinux boot path touches. Anything
// outside faults with the index in the report (batch-1 note: never
// silently accept an MSR).
constexpr std::array<uint32_t, 15> kMsrWhitelist = {
    0xC0000080U, // EFER
    0xC0000081U, // STAR
    0xC0000082U, // LSTAR
    0xC0000083U, // CSTAR
    0xC0000084U, // SFMASK
    0xC0000100U, // FS_BASE
    0xC0000101U, // GS_BASE
    0xC0000102U, // KERNEL_GS_BASE
    0x0000001BU, // APIC_BASE
    0x00000277U, // PAT
    0x000000FEU, // MTRRCAP
    0x000001A0U, // IA32_MISC_ENABLE
    0x00000179U, // MCG_CAP
    0x000006E0U, // IA32_TSC_DEADLINE
    0x00000010U, // TSC
};

[[nodiscard]] bool msr_listed(uint32_t index) {
    return std::ranges::find(kMsrWhitelist, index) != kMsrWhitelist.end();
}

} // namespace

std::optional<uint64_t> Cpu::read_msr(uint32_t index) const {
    if (!msr_listed(index)) {
        return std::nullopt;
    }
    const auto it = msr_.find(index);
    return it != msr_.end() ? std::optional<uint64_t>{it->second} : std::optional<uint64_t>{0};
}

bool Cpu::write_msr(uint32_t index, uint64_t value) {
    if (!msr_listed(index)) {
        return false;
    }
    msr_[index] = value;
    // The segment model reads the bases from members: keep them in sync
    // (arch_prctl sets FS_BASE through wrmsr, and ld.so's TLS lives at
    // fs:0x10 -- a stale zero base faults on the first TLS access).
    switch (index) {
    case 0xC0000080U:
        efer_value_ = value;
        break;
    case 0xC0000100U:
        fs_base_ = value;
        break;
    case 0xC0000101U:
        gs_base_ = value;
        break;
    case 0xC0000102U:
        kernel_gs_base_ = value;
        break;
    default:
        break;
    }
    return true;
}

Cpu::CpuidAnswer Cpu::cpuid(uint32_t leaf, uint32_t subleaf) {
    (void)subleaf; // no strided leaves in the v0 self-description
    CpuidAnswer answer;
    switch (leaf) {
    case 0U:
        answer.eax = 0x0000000DU; // highest standard leaf
        answer.ebx = 0x756E6547U; // "Genu"
        answer.edx = 0x49656E69U; // "ineI"
        answer.ecx = 0x6C65746EU; // "ntel"
        break;
    case 1U: {
        answer.eax = 0x000006F1U; // family 6, model 15, stepping 1
        answer.ebx = 0x00000800U; // brand 0, cache-line 64
        answer.ecx = 0;           // nothing past SSE2 is claimed
        // FPU DE PSE TSC MSR PAE MCE CX8 APIC SEP MTRR PGE CMOV PAT
        // PSE36 CLFSH DS MMX FXSR SSE SSE2
        answer.edx = (1U << 0U) | (1U << 2U) | (1U << 3U) | (1U << 4U) | (1U << 5U) | (1U << 6U) |
                     (1U << 7U) | (1U << 8U) | (1U << 9U) | (1U << 11U) | (1U << 12U) |
                     (1U << 13U) | (1U << 15U) | (1U << 16U) | (1U << 17U) | (1U << 19U) |
                     (1U << 21U) | (1U << 23U) | (1U << 24U) | (1U << 25U) | (1U << 26U);
        break;
    }
    case 7U:
        // No SMEP/SMAP/AVX/etc: only what the executor implements.
        answer.eax = 0;
        break;
    case 0x80000000U:
        answer.eax = 0x80000008U; // highest extended leaf
        answer.ebx = 0x756E6547U;
        answer.edx = 0x49656E69U;
        answer.ecx = 0x6C65746EU;
        break;
    case 0x80000001U:
        // NX (20) | SYSCALL (11) | LM (29)
        answer.edx = (1U << 20U) | (1U << 11U) | (1U << 29U);
        answer.ecx = 0;
        break;
    default:
        break; // known-max leaves beyond this answer zero
    }
    return answer;
}

ExitStatus Cpu::execute_system(const DecodedInsn& insn, uint64_t rip_after) {
    const auto& a = insn.ops[0];
    uint64_t va = 0;
    switch (insn.mnemonic) {
    case Mnemonic::kPushfq:
        if (!push_value(rflags() & width_mask(insn.opsize), insn.opsize)) {
            return exit_;
        }
        break;
    case Mnemonic::kPopfq:
        if (!pop_value(insn.opsize, va)) {
            return exit_;
        }
        set_rflags(va);
        break;
    case Mnemonic::kCli:
        if_ = false;
        break;
    case Mnemonic::kSti:
        if_ = true;
        sti_shadow_ = true; // one instruction runs before IF reopens delivery
        break;
    case Mnemonic::kClac:
        ac_ = false;
        break;
    case Mnemonic::kStac:
        ac_ = true;
        break;
    case Mnemonic::kFninit:
        // DB E3 only (decoder refuses every other 0xDB form): the modeled
        // x87 state is "reset, unused" — no fp stack, no status word.
        break;
    case Mnemonic::kUd2:
        latch(ExitStatus{
            .kind = ExitStatus::Kind::kFault,
            .code = insn_rip_,
            .rip = insn_rip_,
            .detail = static_cast<uint8_t>(FaultId::kUd),
        });
        retire(1);
        return exit_;
    case Mnemonic::kCpuid: {
        const auto answer = cpuid(static_cast<uint32_t>(regs_[index_of(Reg::kRax)]),
                                  static_cast<uint32_t>(regs_[index_of(Reg::kRcx)]));
        write_gpr(Reg::kRax, 4, true, answer.eax);
        write_gpr(Reg::kRbx, 4, true, answer.ebx);
        write_gpr(Reg::kRcx, 4, true, answer.ecx);
        write_gpr(Reg::kRdx, 4, true, answer.edx);
        break;
    }
    case Mnemonic::kRdtsc: {
        // Deterministic virtual clock: TSC derives from icount.
        const uint64_t stamp = icount_;
        write_gpr(Reg::kRax, 4, true, stamp & 0xFFFFFFFFULL);
        write_gpr(Reg::kRdx, 4, true, stamp >> 32U);
        break;
    }
    case Mnemonic::kRdmsr: {
        const auto index = static_cast<uint32_t>(regs_[index_of(Reg::kRcx)]);
        const auto value = read_msr(index);
        if (!value.has_value()) {
            latch(ExitStatus{
                .kind = ExitStatus::Kind::kFault,
                .code = index,
                .rip = insn_rip_,
                .detail = static_cast<uint64_t>(FaultId::kMsr),
            });
            return exit_;
        }
        write_gpr(Reg::kRax, 4, true, *value & 0xFFFFFFFFULL);
        write_gpr(Reg::kRdx, 4, true, *value >> 32U);
        break;
    }
    case Mnemonic::kWrmsr: {
        const auto index = static_cast<uint32_t>(regs_[index_of(Reg::kRcx)]);
        const uint64_t value = (regs_[index_of(Reg::kRdx)] << 32U) | regs_[index_of(Reg::kRax)];
        if (!write_msr(index, value)) {
            latch(ExitStatus{
                .kind = ExitStatus::Kind::kFault,
                .code = index,
                .rip = insn_rip_,
                .detail = static_cast<uint64_t>(FaultId::kMsr),
            });
            return exit_;
        }
        break;
    }
    case Mnemonic::kSwapgs:
        swapgs_state();
        break;
    case Mnemonic::kLgdt:
    case Mnemonic::kLidt: {
        const uint64_t ea = effective_address(a, rip_after);
        uint64_t limit = 0;
        uint64_t base = 0;
        if (!load(ea, 2, limit) || !load(ea + 2U, 8, base)) {
            return exit_;
        }
        if (insn.mnemonic == Mnemonic::kLgdt) {
            gdtr_limit_ = static_cast<uint16_t>(limit);
            gdtr_base_ = base;
        } else {
            idtr_limit_ = static_cast<uint16_t>(limit);
            idtr_base_ = base;
        }
        break;
    }
    case Mnemonic::kSgdt:
    case Mnemonic::kSidt: {
        const uint64_t ea = effective_address(a, rip_after);
        const bool gdt = insn.mnemonic == Mnemonic::kSgdt;
        if (!store(ea, 2, gdt ? gdtr_limit_ : idtr_limit_) ||
            !store(ea + 2U, 8, gdt ? gdtr_base_ : idtr_base_)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kRdrand: {
        // Deterministic: splitmix64 over the machine's own seed. CF=1
        // reports success (a hardware RNG that always delivers).
        rng_state_ += 0x9E3779B97F4A7C15ULL;
        uint64_t z = rng_state_;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        z = z ^ (z >> 31U);
        if (!write_operand(insn, a, z & width_mask(a.size))) {
            return exit_;
        }
        cf_ = true;
        break;
    }
    case Mnemonic::kSldt:
        if (!write_operand(insn, a, ldt_selector_)) {
            return exit_;
        }
        break;
    case Mnemonic::kStr:
        if (!write_operand(insn, a, tss_selector_)) {
            return exit_;
        }
        break;
    case Mnemonic::kLldt: {
        uint64_t value = 0;
        if (!read_operand(insn, a, rip_after, value)) {
            return exit_;
        }
        ldt_selector_ = static_cast<uint16_t>(value);
        break;
    }
    case Mnemonic::kLtr: {
        uint64_t value = 0;
        if (!read_operand(insn, a, rip_after, value)) {
            return exit_;
        }
        tss_selector_ = static_cast<uint16_t>(value);
        break;
    }
    case Mnemonic::kEndbr64:
        break; // indirect-branch landing pad: a defined no-op without CET
    case Mnemonic::kRdsspq:
        // Shadow-stack pointer reads zero while SHSTK is disabled.
        if (!write_operand(insn, a, 0)) {
            return exit_;
        }
        break;
    case Mnemonic::kInvlpg: {
        // Drop the cached translation for the operand address: the
        // kernel rewrites leaf PTEs and relies on this (allocators,
        // copy-on-write, demand paging).
        const uint64_t ea = effective_address(a, rip_after);
        invalidate_page(ea);
        break;
    }
    default:
        return unsupported(insn);
    }
    return exit_; // kRunning: the caller retires the instruction
}

} // namespace micro_forge::arch::x86_64
