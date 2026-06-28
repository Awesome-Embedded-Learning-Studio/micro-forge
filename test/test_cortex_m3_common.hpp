#pragma once

#include <gtest/gtest.h>

#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "memory/bus.hpp"
#include "memory/flat_memory.hpp"
#include "util/logger.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace micro_forge;
using namespace micro_forge::cpu;
using namespace micro_forge::cpu::arm::cortex_m3;

// ── Test fixture ──

class CortexM3Test : public ::testing::Test {
  protected:
    static constexpr addr_t kMemBase = 0x00000000;
    static constexpr addr_t kMemSize = 4096;

    memory::FlatMemory mem_{kMemSize};
    memory::Bus bus_;
    std::unique_ptr<CortexM3CPU> cpu_;

    void SetUp() override {
        ASSERT_TRUE(bus_.map(memory::region(kMemBase, kMemSize, mem_.GetWeak()))
                        .has_value());
        cpu_ = std::make_unique<CortexM3CPU>(&bus_);
    }

    void load_program(const std::vector<uint16_t>& insns, addr_t base = 0) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(insns.data());
        ASSERT_TRUE(mem_.load(base, {bytes, insns.size() * sizeof(uint16_t)})
                        .has_value());
    }

    void reset_cpu() { (void)cpu_->reset(); }
    void start_cpu() { cpu_->launch(); }
    void step_cpu() { (void)cpu_->step(); }

    void set_reg(size_t idx, data_t val) {
        (void)cpu_->set_register_value(idx, val);
    }
    data_t reg(size_t idx) {
        return cpu_->register_value(idx).value_or(0xDEAD);
    }
    void set_pc(addr_t pc) { (void)cpu_->set_pc(pc); }

    void run_until_halt(size_t max_steps = 1000) {
        for (size_t i = 0; i < max_steps; ++i) {
            auto res = cpu_->step();
            if (!res.has_value()) {
                break;
            }
            auto st = cpu_->state();
            if (!st.has_value() || *st != CPU::State::Running) {
                break;
            }
        }
    }
};
