#include "sim/coordinator.hpp"

namespace micro_forge::sim {

SimulationCoordinator::SimulationCoordinator(VirtualClock clock)
    : clock_(std::move(clock)) {}

void SimulationCoordinator::set_cpu(cpu::CPU* cpu) {
    cpu_ = cpu;
}

void SimulationCoordinator::add_tickable(WeakPtr<periph::Device> dev,
                                         size_t domain_index) {
    tickables_.push_back({std::move(dev), domain_index});
}

cpu::CPU::CPUExpected<void> SimulationCoordinator::step() {
    if (cpu_ == nullptr) {
        return std::unexpected(cpu::CPU::CPUError::NotRunning);
    }

    // P2 fast-forward: if the CPU is detected in a busy-wait (HAL_Delay-style
    // poll loop), jump straight to the next timer event instead of stepping
    // the loop. Disabled unless set_fast_forward_enabled(true) AND detection
    // agrees. advance_cycles keeps the cpu cycles ↔ clock lockstep intact.
    if (fast_forward_enabled_ && is_cpu_sleeping_()) {
        uint64_t skip = 0;
        for (const auto& t : tickables_) {
            if (!t.device.IsValid()) {
                continue;
            }
            uint64_t e = t.device->cycles_until_next_event();
            if (e > 0 && (skip == 0 || e < skip)) {
                skip = e;
            }
        }
        if (skip > 0) {
            (void)cpu_->advance_cycles(skip);
            clock_.advance(skip);
            auto c = cpu_->cycles();
            last_cycles_ = c ? *c : last_cycles_ + skip;
            for (const auto& t : tickables_) {
                if (!t.device.IsValid()) {
                    continue;
                }
                uint64_t tk = clock_.consume_ticks(t.domain_index);
                if (tk > 0) {
                    (void)t.device->tick(tk);
                }
            }
            // Fall through, NOT return: the timer IRQ is now pending, but the
            // CPU must still step to enter its handler (SysTick → uwTick++).
            // Skipping the step entirely left the poll loop stuck — HAL_Delay
            // fast-forwarded forever, uwTick never advanced, led.on() never ran.
        }
    }

    // prev is the cycle count after the *previous* step — cached in
    // last_cycles_ to avoid a second virtual cycles() round-trip per step
    // (was 2×/step, now 1×). It starts at 0, matching the post-reset
    // cycles_ == 0 invariant, and the coordinator is the sole driver of
    // cpu->step(), so the two stay in lockstep.
    uint64_t prev = last_cycles_;

    auto step_result = cpu_->step();
    if (!step_result.has_value()) {
        return step_result;
    }

    auto curr_result = cpu_->cycles();
    if (!curr_result.has_value()) {
        return std::unexpected(curr_result.error());
    }
    uint64_t delta = *curr_result - prev;
    last_cycles_ = *curr_result;

    if (delta > 0) {
        clock_.advance(delta);

        for (auto& t : tickables_) {
            if (!t.device.IsValid()) {
                continue;
            }
            uint64_t ticks = clock_.consume_ticks(t.domain_index);
            if (ticks > 0) {
                t.device->tick(ticks);
            }
        }
    }

    return {};
}

bool SimulationCoordinator::is_cpu_sleeping_() const noexcept {
    // P2.a WFI fast-forward: trigger when the CPU is asleep on WFI — the only
    // state an industry-standard simulator fast-forwards (QEMU sleep=no /
    // Simics hypersimulation). Never a busy-wait.
    return cpu_ != nullptr && cpu_->is_sleeping();
}

RunResult SimulationCoordinator::run(size_t max_steps) {
    for (size_t i = 0; i < max_steps; ++i) {
        auto result = step();
        if (!result) {
            return RunResult::StepError;
        }

        auto state_result = cpu_->state();
        if (!state_result) {
            return RunResult::StepError;
        }
        if (*state_result == cpu::CPU::State::Halted) {
            return RunResult::Halted;
        }
        if (*state_result == cpu::CPU::State::Faulted) {
            return RunResult::Faulted;
        }
    }
    return RunResult::Running;
}

} // namespace micro_forge::sim
