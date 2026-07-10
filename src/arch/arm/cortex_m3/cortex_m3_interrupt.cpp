#include "arch/arm/cortex_m3/cortex_m3.hpp"

#include <expected>

namespace micro_forge::cpu::arm::cortex_m3 {

// ── Interrupt handling ──

CPU::CPUExpected<void> CortexM3CPU::write_pc(data_t value) {
    if (in_handler_mode_ && (value & 0xFFFFFFF0u) == 0xFFFFFFF0u) {
        if (value == 0xFFFFFFF1u || value == 0xFFFFFFF9u ||
            value == 0xFFFFFFFDu) {
            return interrupt_return(value);
        }
    }
    return write_reg(15, value & ~1u);
}

CPU::CPUExpected<void> CortexM3CPU::check_and_handle_interrupt() {
    // PRIMASK / FAULTMASK mask all maskable exceptions (SysTick + external
    // IRQs). HardFault / NMI enter via trigger_hardfault(), not through here,
    // so they remain unaffected by these masks.
    const bool globally_masked = (primask_ & 1u) || (faultmask_ & 1u);

    // Active priority: thread mode is lowest (0xFF); in handler mode it is the
    // running exception's priority. An exception may pre-empt only if its
    // preemption-group priority is strictly smaller than the active one. This
    // (rather than an early return in handler mode) is what enables nesting.
    //
    // Thread mode uses 0xFF directly (NOT preempt_priority(0xFF)): that helper
    // truncates to the preempt-group width (e.g. 0xF for 4-bit STM32F1), which
    // equals the lowest-priority exception's preempt (e.g. SysTick at 0xF0 →
    // 0xF). The strict `<` would then block that exception from preempting
    // thread mode, violating ARMv7-M (thread is preempted by ANY exception).
    // 0xFF is larger than any exception preempt (0..0xF), so all pending
    // exceptions preempt thread mode; handler-mode nesting is unaffected.
    const uint8_t active_preempt =
        in_handler_mode_ ? preempt_priority(current_priority_) : 0xFFu;

    // Pick the highest-priority candidate across SysTick and external IRQs.
    bool take_systick = false;
    uint8_t take_irq = 0xFF;
    uint8_t best_preempt = 0xFFu; // smaller value = higher priority

    // SysTick (system exception 15) pends via pending_sys_tick_, bypassing
    // NVIC.
    if (pending_sys_tick_ && !globally_masked) {
        uint8_t sp = preempt_priority(system_exception_priority(15));
        if (sp < active_preempt && sp < best_preempt) {
            best_preempt = sp;
            take_systick = true;
            take_irq = 0xFF;
        }
    }

    // External IRQs (sourced from NVIC).
    if (nvic_ && !globally_masked && nvic_->has_pending_irq()) {
        uint8_t irq_n = nvic_->highest_priority_pending_irq();
        if (irq_n != 0xFF && nvic_->is_enabled(irq_n)) {
            uint8_t irq_pri = nvic_->irq_priority(irq_n);
            uint8_t ip = preempt_priority(irq_pri);
            // BASEPRI masks exceptions whose group priority >= BASEPRI
            // (BASEPRI == 0 disables the mask).
            bool basepri_ok =
                basepri_ == 0 ||
                ip < preempt_priority(static_cast<uint8_t>(basepri_));
            if (basepri_ok && ip < active_preempt && ip < best_preempt) {
                best_preempt = ip;
                take_systick = false;
                take_irq = irq_n;
            }
        }
    }

    if (take_systick) {
        pending_sys_tick_ = false;
        return interrupt_entry_system(15);
    }
    if (take_irq != 0xFF) {
        return interrupt_entry(take_irq);
    }
    return {};
}

CPU::CPUExpected<void>
CortexM3CPU::exception_entry_common(addr_t vector_addr, uint8_t new_priority) {
    // EXC_RETURN reflects the state being left.
    const bool nested = in_handler_mode_;
    const bool thread_use_psp = !nested && (control_ & 0x2u);
    const data_t exc_return = nested           ? 0xFFFFFFF1u
                              : thread_use_psp ? 0xFFFFFFFDu
                                               : 0xFFFFFFF9u;

    // Save the priority we are suspending; restored on return. This stack is
    // what makes nested preemption return to the right active priority.
    active_priorities_.push_back(current_priority_);

    // ITSTATE lives in xPSR on Cortex-M and the hardware-stacked xPSR restores
    // it on exception return.  Our IT decoder keeps the remaining conditions
    // out-of-band, so suspend them explicitly and start the handler outside
    // any interrupted thread/handler IT block.
    suspended_it_states_.push_back(
        {std::move(it_conditions_), it_condition_pos_});
    it_conditions_.clear();
    it_condition_pos_ = 0;

    // Switch active stack to MSP for stacking. Handler mode always uses MSP;
    // if thread mode was on PSP, preserve PSP then load MSP. The active-SP
    // invariant (R13 == active shadow) is maintained because push_stack routes
    // through write_reg(13).
    if (thread_use_psp) {
        psp_ = regs_.read(13).value_or(0);
    }
    in_handler_mode_ = true; // active stack is now MSP
    if (thread_use_psp) {
        auto wr = write_reg(13, msp_);
        if (!wr) {
            return std::unexpected{CPUError::ExceptionEntryFault};
        }
    }

    // Stack the frame {R0,R1,R2,R3,R12,LR,PC,xPSR} (R0 pushed last → lowest
    // address). push_stack routes through write_reg(13) so MSP stays in sync.
    auto push_one = [&](data_t val) -> CPUExpected<void> {
        auto r = push_stack(val);
        if (!r) {
            return std::unexpected{CPUError::ExceptionEntryFault};
        }
        return {};
    };

    if (auto r = push_one(xpsr_); !r) {
        return r;
    }
    if (auto r = push_one(regs_.read(15).value_or(0)); !r) {
        return r;
    }
    if (auto r = push_one(regs_.read(14).value_or(0)); !r) {
        return r;
    }
    if (auto r = push_one(regs_.read(12).value_or(0)); !r) {
        return r;
    }
    if (auto r = push_one(regs_.read(3).value_or(0)); !r) {
        return r;
    }
    if (auto r = push_one(regs_.read(2).value_or(0)); !r) {
        return r;
    }
    if (auto r = push_one(regs_.read(1).value_or(0)); !r) {
        return r;
    }
    if (auto r = push_one(regs_.read(0).value_or(0)); !r) {
        return r;
    }

    auto res = write_reg(14, exc_return);
    if (!res) {
        return res;
    }

    if (!bus_) {
        return std::unexpected{CPUError::ExceptionEntryFault};
    }
    auto handler = bus_->read(vector_addr, Width::Word);
    if (!handler) {
        return std::unexpected{CPUError::ExceptionEntryFault};
    }
    if (*handler == 0) {
        return std::unexpected{CPUError::ExceptionEntryFault};
    }

    res = write_reg(15, *handler & ~1u);
    if (!res) {
        return res;
    }

    current_priority_ = new_priority;
    return {};
}

CPU::CPUExpected<void> CortexM3CPU::interrupt_entry(uint8_t irq_n) {
    addr_t vector_addr =
        vector_table_base_ + 4u * (static_cast<addr_t>(irq_n) + 16);
    uint8_t pri = nvic_ ? nvic_->irq_priority(irq_n) : 0xFFu;
    auto res = exception_entry_common(vector_addr, pri);
    if (!res) {
        return res;
    }
    nvic_->clear_pending(irq_n);
    return {};
}

CPU::CPUExpected<void> CortexM3CPU::interrupt_return(data_t exc_return) {
    // In handler mode the active stack is MSP. Pop through write_reg(13) so MSP
    // stays in sync with R13.
    auto pop_one = [&]() -> CPUExpected<data_t> {
        auto r = pop_stack();
        if (!r) {
            return std::unexpected{CPUError::ExceptionReturnFault};
        }
        return *r;
    };
    auto pop_and_write = [&](uint8_t reg_idx) -> CPUExpected<void> {
        auto v = pop_one();
        if (!v) {
            return std::unexpected{v.error()};
        }
        return write_reg(reg_idx, *v);
    };

    if (auto r = pop_and_write(0); !r) {
        return r;
    }
    if (auto r = pop_and_write(1); !r) {
        return r;
    }
    if (auto r = pop_and_write(2); !r) {
        return r;
    }
    if (auto r = pop_and_write(3); !r) {
        return r;
    }
    if (auto r = pop_and_write(12); !r) {
        return r;
    }
    if (auto r = pop_and_write(14); !r) {
        return r;
    }

    auto pc_val = pop_one();
    if (!pc_val) {
        return std::unexpected{pc_val.error()};
    }
    auto xpsr_val = pop_one();
    if (!xpsr_val) {
        return std::unexpected{xpsr_val.error()};
    }
    xpsr_ = *xpsr_val;

    // Restore the IT block interrupted by this exception.  This must happen
    // before the resumed instruction is fetched; otherwise handler
    // instructions consume the thread's conditions and conditional moves can
    // become unconditional after return.
    if (suspended_it_states_.empty()) {
        return std::unexpected{CPUError::ExceptionReturnFault};
    }
    auto suspended_it = std::move(suspended_it_states_.back());
    suspended_it_states_.pop_back();
    it_conditions_ = std::move(suspended_it.conditions);
    it_condition_pos_ = suspended_it.condition_pos;

    // Restore PC (clear Thumb bit). write_reg(15) writes R15 directly.
    if (auto r = write_reg(15, *pc_val & ~1u); !r) {
        return r;
    }

    // Restore the priority of the context we return to.
    if (!active_priorities_.empty()) {
        current_priority_ = active_priorities_.back();
        active_priorities_.pop_back();
    } else {
        current_priority_ = 0xFFu;
    }

    // Restore mode + active stack per EXC_RETURN.
    if (exc_return == 0xFFFFFFF1u) {
        // Return to nested handler mode: stay in handler mode, keep MSP.
        // (in_handler_mode_ stays true; SPSEL unchanged.)
    } else {
        in_handler_mode_ = false;
        if (exc_return == 0xFFFFFFFDu) {
            // Thread mode on PSP: save MSP, switch to PSP.
            msp_ = regs_.read(13).value_or(0);
            control_ |= 0x2u; // SPSEL = PSP
            if (auto wr = write_reg(13, psp_); !wr) {
                return wr;
            }
        } else {
            // Thread mode on MSP. R13 already holds MSP.
            control_ &= ~0x2u; // SPSEL = MSP
        }
    }

    return {};
}

CPU::CPUExpected<void>
CortexM3CPU::interrupt_entry_system(uint8_t exception_num) {
    addr_t vector_addr = vector_table_base_ + 4u * exception_num;
    uint8_t pri = system_exception_priority(exception_num);
    return exception_entry_common(vector_addr, pri);
}

CPU::CPUExpected<void> CortexM3CPU::trigger_hardfault() {
    if (in_handler_mode_) {
        return std::unexpected{CPUError::ExceptionEntryFault};
    }
    return interrupt_entry_system(3);
}

bool CortexM3CPU::try_escalate_fault(CPUError kind, addr_t pc, uint16_t hw1,
                                     uint16_t hw2, bool is32) {
    record_fault(kind, pc, hw1, hw2, is32);
    if (in_handler_mode_) {
        return false;
    }
    auto res = trigger_hardfault();
    return res.has_value();
}

} // namespace micro_forge::cpu::arm::cortex_m3
