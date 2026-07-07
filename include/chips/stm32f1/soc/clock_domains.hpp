#pragma once

#include "sim/virtual_clock.hpp"

#include <cstdint>

namespace micro_forge::chips::stm32f1 {

// STM32F103 的三个时钟域
enum class ClockDomain : uint8_t {
    Sysclk = 0, // CPU + AHB（SysTick 用这个）
    Apb1 = 1,   // APB1 总线（TIM2-7, USART2-5, I2C, SPI2）
    Apb2 = 2,   // APB2 总线（GPIO, USART1, TIM1, ADC）
};

static constexpr size_t kClockDomainCount = 3;

// STM32F103 复位默认：HSI 8MHz，无 PLL，所有总线无分频
static constexpr sim::DomainConfig stm32f103_default_clocks[] = {
    {8'000'000}, // Sysclk
    {8'000'000}, // Apb1
    {8'000'000}, // Apb2
};

inline size_t domain_index(ClockDomain d) {
    return static_cast<size_t>(d);
}

} // namespace micro_forge::chips::stm32f1
