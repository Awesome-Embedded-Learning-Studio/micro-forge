#include <gtest/gtest.h>

#include "chips/stm32f1/soc/clock_domains.hpp"
#include "sim/virtual_clock.hpp"

using namespace micro_forge;
using namespace sim;
using namespace chips::stm32f1;

// ── 构造与基础 ──

TEST(VirtualClock, DefaultConstruction) {
    VirtualClock clk(stm32f103_default_clocks);

    EXPECT_EQ(clk.domain_count(), 3);
    EXPECT_EQ(clk.sysclk_freq_hz(), 8'000'000);
    EXPECT_EQ(clk.domain_freq_hz(domain_index(ClockDomain::Sysclk)), 8'000'000);
    EXPECT_EQ(clk.domain_freq_hz(domain_index(ClockDomain::Apb1)), 8'000'000);
    EXPECT_EQ(clk.domain_freq_hz(domain_index(ClockDomain::Apb2)), 8'000'000);
}

TEST(VirtualClock, AdvanceOneCycle) {
    VirtualClock clk(stm32f103_default_clocks);

    clk.advance(1);

    // 所有域同频，每个域各得 1 tick
    EXPECT_EQ(clk.consume_ticks(domain_index(ClockDomain::Sysclk)), 1u);
    EXPECT_EQ(clk.consume_ticks(domain_index(ClockDomain::Apb1)), 1u);
    EXPECT_EQ(clk.consume_ticks(domain_index(ClockDomain::Apb2)), 1u);
}

TEST(VirtualClock, ConsumeClears) {
    VirtualClock clk(stm32f103_default_clocks);

    clk.advance(10);
    EXPECT_EQ(clk.consume_ticks(domain_index(ClockDomain::Sysclk)), 10u);
    EXPECT_EQ(clk.consume_ticks(domain_index(ClockDomain::Sysclk)), 0u);
}

TEST(VirtualClock, TotalNs) {
    VirtualClock clk(stm32f103_default_clocks);

    clk.advance(1);
    // 8 MHz → period = 125 ns
    EXPECT_EQ(clk.total_ns(), 125u);
}

TEST(VirtualClock, TotalNsAccumulatesFractionalSysclkPeriods) {
    DomainConfig cfg[] = {{72'000'000}};
    VirtualClock clk(cfg);

    clk.advance(72);

    EXPECT_EQ(clk.total_ns(), 1000u);
}

// ── 频率差异 ──

TEST(VirtualClock, DifferentDomainFrequencies) {
    // SYSCLK = 72 MHz, APB1 = 36 MHz, APB2 = 72 MHz
    DomainConfig cfg[] = {
        {72'000'000}, // Sysclk
        {36'000'000}, // Apb1
        {72'000'000}, // Apb2
    };
    VirtualClock clk(cfg);

    // 推进 72 个 CPU 周期 = 72 * (1e9 / 72e6) = 1000 ns
    clk.advance(72);

    // Sysclk: 1000ns / 13.888...ns ≈ 72 ticks (整除)
    EXPECT_EQ(clk.consume_ticks(domain_index(ClockDomain::Sysclk)), 72u);
    // Apb1: 1000ns / 27.777...ns ≈ 36 ticks
    EXPECT_EQ(clk.consume_ticks(domain_index(ClockDomain::Apb1)), 36u);
    // Apb2: same as Sysclk
    EXPECT_EQ(clk.consume_ticks(domain_index(ClockDomain::Apb2)), 72u);
}

// ── 动态频率切换 ──

TEST(VirtualClock, SetDomainFreq) {
    VirtualClock clk(stm32f103_default_clocks);

    // 初始 8MHz，推进 8 周期 → 各域 8 ticks
    clk.advance(8);
    EXPECT_EQ(clk.consume_ticks(domain_index(ClockDomain::Apb1)), 8u);
    EXPECT_EQ(clk.consume_ticks(domain_index(ClockDomain::Sysclk)), 8u);

    // 切 Apb1 到 4MHz
    clk.set_domain_freq(domain_index(ClockDomain::Apb1), 4'000'000);
    EXPECT_EQ(clk.domain_freq_hz(domain_index(ClockDomain::Apb1)), 4'000'000);

    // 再推进 8 周期 → Apb1 应得 4 ticks，Sysclk 仍 8 ticks
    clk.advance(8);
    EXPECT_EQ(clk.consume_ticks(domain_index(ClockDomain::Apb1)), 4u);
    EXPECT_EQ(clk.consume_ticks(domain_index(ClockDomain::Sysclk)), 8u);
}

TEST(VirtualClock, InvalidDomainIndexIsSafe) {
    VirtualClock clk(stm32f103_default_clocks);

    clk.advance(10);
    EXPECT_EQ(clk.consume_ticks(clk.domain_count()), 0u);
    EXPECT_EQ(clk.domain_freq_hz(clk.domain_count()), 0u);

    clk.set_domain_freq(clk.domain_count(), 1'000'000);
    EXPECT_EQ(clk.domain_freq_hz(domain_index(ClockDomain::Sysclk)),
              8'000'000u);
}

// ── 余数累积（72MHz 不整除） ──

TEST(VirtualClock, ResidualAccumulation) {
    // 只用 SYSCLK 72MHz，验证余数不丢
    DomainConfig cfg[] = {{72'000'000}};
    VirtualClock clk(cfg);

    // advance(1) = 13.888... ns，不够一个 72MHz tick？
    // 不，Sysclk 就是 CPU 域，advance 的单位就是 SYSCLK 周期
    // 1 个 CPU 周期 = 1e9/72e6 ns = 13.888... ns
    // 1 个 Sysclk tick = 13.888... ns
    // 所以 advance(1) 应产生 1 tick（period == sysclk_period）
    clk.advance(1);
    EXPECT_EQ(clk.consume_ticks(0), 1u);
}
