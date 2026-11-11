/**
 * @file syscall_impl.cpp
 * @brief The syscall instruction at both privilege levels (batch 6d).
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/cpu.hpp"

namespace micro_forge::arch::x86_64 {

ExitStatus Cpu::execute_syscall(uint64_t rip_after) {
    if (cpl_ == 3) {
        // User-mode syscall: hardware saves the return address in RCX
        // and the flags in R11, masks IF/TF, and jumps to LSTAR. The
        // kernel entry then switches stacks and dispatches.
        const auto lstar = read_msr(0xC0000082U);
        if (!lstar.has_value() || *lstar == 0) {
            latch(ExitStatus{
                .kind = ExitStatus::Kind::kUnsupported,
                .code = insn_rip_,
                .rip = insn_rip_,
                .detail = regs_[index_of(Reg::kRax)],
            });
            return exit_;
        }
        // insn_rip_ is the SYSCALL start in both the cold path (execute_insn
        // runs before rip_ advances) and the decode-cache hit path (cpu.cpp
        // moves rip_ to rip_after before execute_cached), so it - not rip_ -
        // is the RCX contract: return address = syscall + 2.
        regs_[index_of(Reg::kRcx)] = insn_rip_ + 2U;
        regs_[index_of(Reg::kR11)] = rflags();
        set_rflags(rflags() & ~0x300ULL); // clear IF | TF
        rip_ = *lstar;
        cpl_ = 0;
        retire(1);
        return exit_;
    }
    rip_ = rip_after; // write must continue past the syscall (batch-5 review)
    const uint64_t nr = regs_[index_of(Reg::kRax)];
    switch (nr) {
    case 1: { // write(fd, buf, count): collect into the console sink.
        const uint64_t buf = regs_[index_of(Reg::kRsi)];
        const uint64_t count = regs_[index_of(Reg::kRdx)];
        for (uint64_t i = 0; i < count; ++i) {
            uint64_t byte = 0;
            if (!load(buf + i, 1, byte)) {
                return exit_;
            }
            console_.push_back(static_cast<char>(byte));
        }
        regs_[index_of(Reg::kRax)] = count; // bytes written
        retire(1);
        return exit_;
    }
    case 60:  // exit
    case 231: // exit_group
        latch(ExitStatus{
            .kind = ExitStatus::Kind::kExit,
            .code = regs_[index_of(Reg::kRdi)],
            .rip = insn_rip_,
            .detail = nr,
        });
        retire(1);
        return exit_;
    default:
        // Detail carries the syscall number: the batch-3 surface is
        // write/exit/exit_group, anything else names its caller precisely.
        latch(ExitStatus{
            .kind = ExitStatus::Kind::kUnsupported,
            .code = nr,
            .rip = insn_rip_,
            .detail = nr,
        });
        return exit_;
    }
}

} // namespace micro_forge::arch::x86_64
