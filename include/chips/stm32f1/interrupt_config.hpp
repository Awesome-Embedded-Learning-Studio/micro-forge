#pragma once

#include "cpu/intr.hpp"
#include "memory/bus.hpp"
#include "periph/nvic.hpp"
#include "periph/scb.hpp"
#include "periph/systick.hpp"

namespace micro_forge::chips::stm32f1 {

// STM32F103 external IRQ numbers (Cortex-M exception base is 16, so TIM2 IRQ 28
// lives at vector-table index 16+28 = 44). Source: STM32F1 vector table.
inline constexpr intr::intr_n_t kTim2Irqn = 28;

Expected<void> configure_interrupt_devices(memory::Bus& bus,
                                           periph::NvicPeripheral& nvic,
                                           periph::SysTickPeripheral& systick,
                                           periph::ScbPeripheral& scb);

} // namespace micro_forge::chips::stm32f1
