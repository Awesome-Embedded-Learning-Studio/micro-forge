// Control-flow JIT helpers: ret/call/jmp/jcc/leave lower into thunks that
// record the block exit target (jit_next_rip_) so run_jit_block can jump
// instead of falling through. This file pairs with jit_cpu.cpp.
#include <micro_forge/arch/x86_64/cpu.hpp>

#include <micro_forge/base/jit.hpp>

namespace micro_forge::arch::x86_64 {
namespace {
// Thunks keep the JIT ABI 1:1 with the helper declarations in jit_templates.
extern "C" void mf_jit_ret(void* cpu, uint64_t sp, uint64_t my_rip) {
    static_cast<Cpu*>(cpu)->jit_ret(sp, my_rip);
}

extern "C" void mf_jit_leave(void* cpu) {
    static_cast<Cpu*>(cpu)->jit_leave();
}

extern "C" void mf_jit_jmp(void* cpu, uint64_t target) {
    static_cast<Cpu*>(cpu)->jit_jmp(target);
}

extern "C" void mf_jit_jcc(void* cpu, uint64_t taken, uint64_t fall, unsigned cc) {
    static_cast<Cpu*>(cpu)->jit_jcc(taken, fall, static_cast<Cc>(cc));
}

extern "C" void mf_jit_call(void* cpu, uint64_t target, uint64_t my_rip, uint64_t ret_addr) {
    static_cast<Cpu*>(cpu)->jit_call(target, my_rip, ret_addr);
}
} // namespace
void Cpu::jit_ret(uint64_t sp, uint64_t my_rip) {
    // Faulted stack read: rsp stays put; the interpreter pass re-delivers.
    const uint64_t target = jit_load(sp, 8);
    const bool ok = !exception_injected_;
    if (ok) {
        regs_[index_of(Reg::kRsp)] = sp + 8U;
    }
    jit_next_rip_ = ok ? target : my_rip;
    jit_suspend_ = !ok; // retry in the interpreter on a faulted pop
    jit_next_set_ = true;
}

void Cpu::jit_call(uint64_t target, uint64_t my_rip, uint64_t ret_addr) {
    // A failed push latches the fault; suspend gives the interpreter one
    // pass at the call itself so the fault re-raises like the slow path.
    const bool ok = push_value(ret_addr, 8);
    jit_next_rip_ = ok ? target : my_rip;
    jit_suspend_ = !ok; // retry in the interpreter on a faulted push
    jit_next_set_ = true;
}

void Cpu::jit_jmp(uint64_t target) {
    jit_next_rip_ = target;
    jit_next_set_ = true;
}

void Cpu::jit_jcc(uint64_t taken, uint64_t fall, Cc cc) {
    jit_next_rip_ = condition(cc) ? taken : fall;
    jit_next_set_ = true;
}

void Cpu::jit_leave() {
    regs_[index_of(Reg::kRsp)] = regs_[index_of(Reg::kRbp)];
    uint64_t value = 0;
    (void)pop_value(8, value);
    write_gpr(Reg::kRbp, 8, true, value);
}

} // namespace micro_forge::arch::x86_64
