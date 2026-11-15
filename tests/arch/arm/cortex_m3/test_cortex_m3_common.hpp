#pragma once

#include <gtest/gtest.h>

#include "micro_forge/arch/arm/cortex_m3/cortex_m3.hpp"
#include "micro_forge/arch/arm/cortex_m3/cortex_m3_reset.hpp"
#include "micro_forge/arch/base/memory_access.hpp"
#include "micro_forge/kernel/bus/bus.hpp"
#include "micro_forge/kernel/memory/ram_region.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace micro_forge;
using namespace micro_forge::device;
using namespace micro_forge::arch::cpu;
using namespace micro_forge::arch::arm::cortex_m3;

class CortexM3Test : public ::testing::Test {
protected:
    static constexpr addr_t kMemBase = 0x00000000;
    static constexpr addr_t kMemSize = 4096;

    kernel::RamRegion mem_{kMemSize};
    kernel::Bus bus_;
    arch::MemoryAccess access_{bus_};
    std::unique_ptr<CortexM3CPU> cpu_;

    void SetUp() override {
        ASSERT_TRUE(bus_.map(kMemBase, kMemSize, mem_).has_value());
        cpu_ = std::make_unique<CortexM3CPU>(&access_);
    }

    void load_program(const std::vector<uint16_t>& insns, addr_t base = 0) {
        for (std::size_t i = 0; i < insns.size(); ++i) {
            ASSERT_TRUE(bus_.write_u16(base + i * 2, insns[i]).has_value());
        }
    }

    void reset_cpu() { ASSERT_TRUE(cpu_->reset().has_value()); }
    void start_cpu() { cpu_->launch(); }
    void step_cpu() { ASSERT_TRUE(cpu_->step().has_value()); }

    void set_reg(size_t idx, data_t val) {
        ASSERT_TRUE(cpu_->set_register_value(idx, val).has_value());
    }
    data_t reg(size_t idx) { return cpu_->register_value(idx).value_or(0xDEAD); }
    void set_pc(addr_t pc) { ASSERT_TRUE(cpu_->set_pc(pc).has_value()); }

    void run_until_halt(size_t max_steps = 1000) {
        for (size_t i = 0; i < max_steps; ++i) {
            auto res = cpu_->step();
            if (!res.has_value()) {
                break;
            }
            auto st = cpu_->state();
            if (!st.has_value() || *st != CPU::State::kRunning) {
                break;
            }
        }
    }
};
