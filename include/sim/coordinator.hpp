#pragma once

#include "cpu/cpu.hpp"
#include "periph/device.hpp"
#include "sim/virtual_clock.hpp"
#include "util/weak_ptr/weak_ptr.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace micro_forge::sim {

enum class RunResult { Running, Halted, Faulted, StepError };

struct Tickable {
    WeakPtr<periph::Device> device;
    size_t domain_index;
};

class SimulationCoordinator {
  public:
    explicit SimulationCoordinator(VirtualClock clock);

    void set_cpu(cpu::CPU* cpu);

    // 注册外设及其所属时钟域（domain_index 由芯片配置层提供）
    void add_tickable(WeakPtr<periph::Device> dev, size_t domain_index);

    // 执行一个 step：CPU step + tick 传播
    cpu::CPU::CPUExpected<void> step();

    // 便利：运行到 halted/faulted 或 max_steps
    RunResult run(size_t max_steps = SIZE_MAX);

    VirtualClock& clock() { return clock_; }

    // P2.a WFI fast-forward (emu_busy_wait_research / QEMU sleep=no / Simics
    // hypersimulation): when enabled AND the CPU is asleep (WFI), skip ahead
    // to the next pending exception instead of busy-stepping. Default OFF.
    void set_fast_forward_enabled(bool on) noexcept { fast_forward_enabled_ = on; }
    bool fast_forward_enabled() const noexcept { return fast_forward_enabled_; }

  private:
    bool is_cpu_sleeping_() const noexcept; // P2.a: CPU suspended on WFI?

    bool fast_forward_enabled_ = false;
    cpu::CPU* cpu_ = nullptr; // raw ptr — the CPU outlives the coordinator
                              // (both are SoC members; no reference cycle to
                              // break). A WeakPtr here cost an IsValid +
                              // control-block deref every step.
    VirtualClock clock_;
    std::vector<Tickable> tickables_;
    uint64_t last_cycles_ = 0;
};

} // namespace micro_forge::sim
