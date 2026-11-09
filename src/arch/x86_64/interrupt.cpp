/**
 * @file interrupt.cpp
 * @brief Vector delivery and the IRET return path (batch 6c).
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/cpu.hpp"

namespace micro_forge::arch::x86_64 {

void Cpu::deliver_interrupt() {
    if (!if_ || exit_.kind != ExitStatus::Kind::kRunning) {
        return; // queued until IF opens (level-style, never lost silently?)
    }
    // A missing IDT drops the request: nothing to deliver to yet.
    if (idtr_base_ == 0) {
        interrupt_pending_ = false;
        return;
    }
    uint64_t gate_lo = 0;
    uint64_t gate_hi = 0;
    const uint64_t gate_offset = idtr_base_ + (static_cast<uint64_t>(pending_vector_) * 16U);
    if (!load(gate_offset, 8, gate_lo) || !load(gate_offset + 8U, 8, gate_hi)) {
        interrupt_pending_ = false;
        return;
    }
    const uint64_t offset_lo = gate_lo & 0xFFFFULL;
    const auto type_attr = static_cast<uint8_t>((gate_lo >> 40U) & 0xFFULL);
    const uint64_t offset_mid = (gate_lo >> 48U) & 0xFFFFULL;
    const uint64_t offset_hi = gate_hi & 0xFFFFFFFFULL;
    const uint64_t handler = offset_lo | (offset_mid << 16U) | (offset_hi << 32U);
    const bool present = (type_attr & 0x80U) != 0;
    const unsigned gate_type = type_attr & 0x0FU;
    if (!present || (gate_type != 0xE && gate_type != 0xF)) {
        interrupt_pending_ = false;
        return; // not a valid 64-bit interrupt/trap gate
    }
    // 64-bit interrupt frame: SS, RSP, RFLAGS, CS, RIP (always five
    // qwords, even without a privilege change). Snapshot RSP and the
    // flags BEFORE pushing: push_value already moves the stack pointer.
    const bool from_user = cpl_ == 3;
    const uint64_t saved_rsp = regs_[index_of(Reg::kRsp)];
    const uint64_t saved_flags = rflags();
    if (from_user) {
        // Privilege change: the hardware switches to TSS.RSP0.
        const uint64_t kernel_rsp = kernel_stack_top();
        if (kernel_rsp != 0) {
            regs_[index_of(Reg::kRsp)] = kernel_rsp;
        }
    }
    const uint64_t frame_cs = from_user ? 0x33ULL : 0x08ULL;
    const uint64_t frame_ss = from_user ? 0x2BULL : 0x08ULL;
    if (!push_value(frame_ss, 8) || !push_value(saved_rsp, 8) || !push_value(saved_flags, 8) ||
        !push_value(frame_cs, 8) || !push_value(rip_, 8)) {
        interrupt_pending_ = false;
        return;
    }
    rip_ = handler;
    if (gate_type == 0xE) {
        if_ = false; // interrupt gates clear IF; trap gates leave it
    }
    interrupt_pending_ = false;
}

ExitStatus Cpu::execute_iretq() {
    // 64-bit IRET always pops the five-qword frame.
    uint64_t rip = 0;
    uint64_t cs = 0;
    uint64_t flags = 0;
    uint64_t rsp = 0;
    uint64_t ss = 0;
    (void)cs;
    (void)ss;
    if (!pop_value(8, rip) || !pop_value(8, cs) || !pop_value(8, flags) || !pop_value(8, rsp) ||
        !pop_value(8, ss)) {
        return exit_;
    }
    set_rflags(flags);
    regs_[index_of(Reg::kRsp)] = rsp;
    rip_ = rip;
    // CS carries the target ring (RPL bits 0-1): user code returns to
    // CPL 3, kernel code stays at 0.
    cpl_ = (cs & 0x3U) == 0x3U ? 3U : 0U;
    if (rip >= 0x10000000ULL && rip < 0x20000000ULL) {
    }
    if (cpl_ == 3) {
        // Returning to user mode means every nested exception is done:
        // reset the depth. Counting +1 per raise against two iret legs
        // per delivery drifts and eventually trips the recursion guard.
        exception_depth_ = 0;
    }
    return exit_;
}

TranslateResult Cpu::translate_access(uint64_t vaddr, bool write, bool instruction,
                                      uint64_t fault_rip) {
    const uint64_t cr3 = cr_[2];
    const uint64_t cr0 = cr_[0];
    const uint64_t efer_value = efer_value_;
    TranslateResult result =
        mmu_.translate(vaddr, write, instruction, cpl_ == 3, cr3, cr0, efer_value);
    if (!result.ok) {
        cr2_ = vaddr;
        cr_[1] = vaddr; // CR2 register: the kernel reads it with mov rax,cr2
        pf_error_ = result.error;
        // User faults and kernel faults with a live IDT raise #PF; the
        // kernel's resolver fills the leaf and iretq returns. Only a
        // machine without an IDT latches a terminal fault.
        if (raise_page_fault(result.error, fault_rip)) {
            exception_injected_ = true;
        }
    }
    return result;
}

uint64_t Cpu::segment_address(const DecodedInsn& insn, uint64_t ea) const {
    if (insn.seg == Seg::kFs) {
        return fs_base_ + ea;
    }
    if (insn.seg == Seg::kGs) {
        return gs_base_ + ea;
    }
    return ea;
}

uint64_t Cpu::kernel_stack_top() {
    // arm_task_entry programs TSS.rsp0 = task stack top (direct map) on
    // every switch; GS_BASE offset 0 mirrors it for the syscall entry.
    // Both are VIRTUAL addresses: translation goes through the MMU
    // directly (never load(): a fault here would recurse).
    const uint64_t efer_value = efer_value_;
    const auto translate = [&](uint64_t vaddr, uint64_t& paddr) {
        const TranslateResult result =
            mmu_.translate(vaddr, false, false, false, cr_[2], cr_[0], efer_value);
        if (!result.ok) {
            return false;
        }
        paddr = result.paddr;
        return true;
    };
    const auto read_at = [&](uint64_t paddr, uint64_t& value) {
        const auto word = bus_.read_u64(paddr);
        if (!word.has_value()) {
            return false;
        }
        value = *word;
        return true;
    };
    // TSS.rsp0 first: the GDT descriptor's selector comes from ltr.
    if (tss_selector_ != 0 && gdtr_base_ != 0) {
        const uint64_t gdt_entry = gdtr_base_ + (tss_selector_ & ~7ULL);
        uint64_t lo_paddr = 0;
        if (translate(gdt_entry, lo_paddr)) {
            uint64_t lo = 0;
            uint64_t hi = 0;
            if (read_at(lo_paddr, lo) && read_at(lo_paddr + 8U, hi)) {
                const uint64_t base = ((lo >> 16U) & 0xFFFFULL) | (((lo >> 32U) & 0xFFULL) << 16U) |
                                      (((lo >> 56U) & 0xFFULL) << 24U) |
                                      ((hi & 0xFFFFFFFFULL) << 32U);
                uint64_t rsp0_paddr = 0;
                uint64_t rsp0 = 0;
                if (base != 0 && translate(base + 4U, rsp0_paddr) && read_at(rsp0_paddr, rsp0) &&
                    rsp0 != 0) {
                    return rsp0;
                }
            }
        }
    }
    // Fallback: the per-CPU block's kernel_rsp at GS_BASE offset 0.
    const auto gs_msr = read_msr(0xC0000101U);
    if (gs_msr.has_value() && *gs_msr != 0) {
        uint64_t paddr = 0;
        uint64_t stack = 0;
        if (translate(*gs_msr, paddr) && read_at(paddr, stack) && stack != 0) {
            return stack;
        }
    }
    return 0;
}

bool Cpu::raise_page_fault(uint64_t error, uint64_t fault_rip) {
    if (idtr_base_ == 0) {
        return false; // no IDT yet: a terminal fault is the honest answer
    }
    if (exception_depth_ >= 16) {
        return false; // runaway nesting: latch instead of recursing forever
    }
    ++exception_depth_;
    // Probe: snapshot the faulting instruction's bytes (direct MMU read,
    // never load(): a fault here would recurse) and its length.
    last_fault_length_ = cur_length_;
    const uint64_t efer_value = efer_value_;
    last_fault_bytes_.fill(0);
    for (unsigned i = 0; i < 16; ++i) {
        const TranslateResult tr =
            mmu_.translate(insn_rip_ + i, false, true, false, cr_[2], cr_[0], efer_value);
        if (!tr.ok) {
            break;
        }
        const auto byte = bus_.read_u8(tr.paddr);
        if (!byte.has_value()) {
            break;
        }
        last_fault_bytes_[i] = *byte;
    }
    const bool from_user = cpl_ == 3;
    // The handler runs in ring 0: the IDT read must not be filtered by
    // user-page permissions (the kernel's IDT has U=0).
    cpl_ = 0;
    const uint64_t gate_offset = idtr_base_ + (14ULL * 16U);
    uint64_t gate_lo = 0;
    uint64_t gate_hi = 0;
    if (!load(gate_offset, 8, gate_lo) || !load(gate_offset + 8U, 8, gate_hi)) {
        --exception_depth_;
        return false;
    }
    const uint64_t offset_lo = gate_lo & 0xFFFFULL;
    const auto type_attr = static_cast<uint8_t>((gate_lo >> 40U) & 0xFFULL);
    const uint64_t offset_mid = (gate_lo >> 48U) & 0xFFFFULL;
    const uint64_t offset_hi = gate_hi & 0xFFFFFFFFULL;
    const uint64_t handler = offset_lo | (offset_mid << 16U) | (offset_hi << 32U);
    if ((type_attr & 0x80U) == 0 || (type_attr & 0x0FU) != 0xE) {
        --exception_depth_;
        return false; // #PF must be a present interrupt gate
    }
    // Exception frame: the five-qword interrupt frame plus the error
    // code on top (the kernel's stub pops it).
    const uint64_t saved_rsp = regs_[index_of(Reg::kRsp)];
    const uint64_t saved_flags = rflags();
    if (from_user) {
        // Privilege change: the hardware switches to TSS.RSP0. Without
        // it the frame lands on a user stack that may itself be
        // demand-paged -- the batch-6 review's recursion.
        const uint64_t kernel_rsp = kernel_stack_top();
        if (kernel_rsp != 0) {
            regs_[index_of(Reg::kRsp)] = kernel_rsp;
        }
    }
    // CS/SS carry the interrupted ring: user CS/SS (0x33/0x2B) when the
    // fault came from CPL 3, kernel 0x08 otherwise.
    const uint64_t cs = from_user ? 0x33ULL : 0x08ULL;
    const uint64_t ss = from_user ? 0x2BULL : 0x08ULL;
    if (!push_value(ss, 8) || !push_value(saved_rsp, 8) || !push_value(saved_flags, 8) ||
        !push_value(cs, 8) || !push_value(fault_rip, 8) || !push_value(error, 8)) {
        --exception_depth_;
        return false;
    }
    rip_ = handler;
    if_ = false;
    cpl_ = 0;
    ++exception_depth_;
    return true;
}

ExitStatus Cpu::execute_return(const DecodedInsn& insn) {
    if (insn.mnemonic == Mnemonic::kIretq) {
        return execute_iretq();
    }
    if (insn.mnemonic == Mnemonic::kLretq) {
        return execute_lretq();
    }
    return execute_sysretq();
}

ExitStatus Cpu::execute_sysretq() {
    // SYSRET returns to user mode: RIP from RCX, RFLAGS from R11.
    rip_ = regs_[index_of(Reg::kRcx)];
    set_rflags(regs_[index_of(Reg::kR11)]);
    cpl_ = 3;
    return exit_;
}

ExitStatus Cpu::execute_lretq() {
    // RETF in 64-bit mode pops RIP and CS (operand size is always 64).
    uint64_t rip = 0;
    uint64_t cs = 0;
    (void)cs;
    if (!pop_value(8, rip) || !pop_value(8, cs)) {
        return exit_;
    }
    rip_ = rip;
    return exit_;
}

} // namespace micro_forge::arch::x86_64
