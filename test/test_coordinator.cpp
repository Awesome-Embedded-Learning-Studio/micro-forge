#include <gtest/gtest.h>

#include "chips/stm32f1/clock_domains.hpp"
#include "sim/coordinator.hpp"
#include "sim/virtual_clock.hpp"

#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "cpu/cpu.hpp"
#include "memory/bus.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"

using namespace micro_forge;
using namespace sim;
using namespace chips::stm32f1;

// ── 最小 mock：记录 tick 调用 ──

class TickCounter : public periph::Device {
  public:
    Expected<data_t> read(addr_t, Width) override {
        return std::unexpected(BusError::Unmapped);
    }
    Expected<void> write(addr_t, data_t, Width) override {
        return std::unexpected(BusError::Unmapped);
    }
    std::string_view name() const noexcept override { return "TickCounter"; }

    void tick(uint64_t cycles) override {
        total_ticks_ += cycles;
        tick_call_count_++;
    }

    uint64_t total_ticks() const { return total_ticks_; }
    uint64_t tick_call_count() const { return tick_call_count_; }

    WeakPtr<TickCounter> GetWeak() { return weak_factory_.GetWeakPtr(); }

  private:
    uint64_t total_ticks_ = 0;
    uint64_t tick_call_count_ = 0;
    WeakPtrFactory<TickCounter> weak_factory_{this};
};

// ── 测试 ──

class CoordinatorTest : public ::testing::Test {
  protected:
    void SetUp() override {
        cpu_ = std::make_unique<cpu::arm::cortex_m3::CortexM3CPU>(&bus_);
        (void)cpu_->reset();
        cpu_->launch();

        VirtualClock clk(stm32f103_default_clocks);
        coordinator_ = std::make_unique<SimulationCoordinator>(std::move(clk));
        coordinator_->set_cpu(cpu_->GetWeak());
    }

    memory::Bus bus_;
    std::unique_ptr<cpu::arm::cortex_m3::CortexM3CPU> cpu_;
    std::unique_ptr<SimulationCoordinator> coordinator_;
};

TEST_F(CoordinatorTest, StepCallsTick) {
    auto counter = std::make_unique<TickCounter>();
    coordinator_->add_tickable(counter->GetWeak(),
                               domain_index(ClockDomain::Sysclk));

    (void)coordinator_->step();

    auto cycles = cpu_->cycles();
    if (cycles.has_value() && cycles.value() > 0) {
        EXPECT_GT(counter->total_ticks(), 0u);
        EXPECT_GT(counter->tick_call_count(), 0u);
    }
}

TEST_F(CoordinatorTest, TickCountEqualsCpuCycles) {
    auto counter = std::make_unique<TickCounter>();
    coordinator_->add_tickable(counter->GetWeak(),
                               domain_index(ClockDomain::Sysclk));

    uint64_t prev = cpu_->cycles().value();

    (void)coordinator_->step();

    uint64_t delta = cpu_->cycles().value() - prev;
    EXPECT_EQ(counter->total_ticks(), delta);
}

TEST_F(CoordinatorTest, NullDeviceDoesNotCrash) {
    auto counter = std::make_unique<TickCounter>();
    auto weak = counter->GetWeak();
    coordinator_->add_tickable(weak, domain_index(ClockDomain::Sysclk));

    // 销毁 counter，WeakPtr 失效
    counter.reset();

    // 不应崩溃
    (void)coordinator_->step();
}
