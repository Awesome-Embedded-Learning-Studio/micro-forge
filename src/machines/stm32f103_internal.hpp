/**
 * @file stm32f103_internal.hpp
 * @brief Stable, machine-owned composition shared by runtime and ELF loading.
 * @version 0.1.0
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "micro_forge/arch/arm/cortex_m3/cortex_m3.hpp"
#include "micro_forge/arch/base/memory_access.hpp"
#include "micro_forge/device/base/device_region.hpp"
#include "micro_forge/device/base/hooks/event_bus.hpp"
#include "micro_forge/device/arm/nvic.hpp"
#include "micro_forge/device/arm/scb.hpp"
#include "micro_forge/device/stm32f1/stm32f1_afio.hpp"
#include "micro_forge/device/stm32f1/stm32f1_exti.hpp"
#include "micro_forge/device/stm32f1/stm32f1_flash.hpp"
#include "micro_forge/device/stm32f1/stm32f1_gpio.hpp"
#include "micro_forge/device/stm32f1/stm32f1_rcc.hpp"
#include "micro_forge/device/stm32f1/stm32f1_timer.hpp"
#include "micro_forge/device/stm32f1/stm32f1_usart.hpp"
#include "micro_forge/device/arm/systick.hpp"
#include "micro_forge/kernel/scheduler.hpp"
#include "micro_forge/machines/stm32f103.hpp"

namespace micro_forge::machines {

struct Stm32F103Machine::Impl {
    Impl();
    std::optional<std::span<uint8_t>> image_window(uint64_t address, uint64_t size);
    void tick_devices(uint64_t elapsed);
    void reset_devices();
    RunResult fault(arch::cpu::CPU::CPUError error);
    arch::cpu::CPU::CPUExpected<bool> handle_breakpoint(uint8_t immediate);

    device::FlashRegion flash{kFlashSize};
    kernel::RamRegion sram{kSramSize};
    kernel::Bus bus;
    arch::MemoryAccess memory{bus};
    kernel::Scheduler scheduler;
    device::arm::NvicPeripheral nvic;
    device::arm::ScbPeripheral scb;
    device::arm::SysTickPeripheral systick;
    device::stm32f1::Stm32f1Rcc rcc;
    device::stm32f1::Stm32f1Afio afio;
    device::stm32f1::Stm32f1Exti exti;
    device::stm32f1::Stm32f1Flash flash_interface;
    device::stm32f1::Stm32f1Gpio gpioa{'A'};
    device::stm32f1::Stm32f1Gpio gpiob{'B'};
    device::stm32f1::Stm32f1Gpio gpioc{'C'};
    device::stm32f1::Stm32f1Usart usart1;
    device::stm32f1::Stm32f1Usart usart2;
    device::stm32f1::Stm32f1Timer tim2;
    device::stm32f1::Stm32f1Timer tim3;
    device::hooks::EventBus events;
    device::BitBandRegion sram_bitband{bus, kSramBase};
    device::BitBandRegion peripheral_bitband{bus, 0x4000'0000};
    std::array<device::DeviceRegion, 14> regions = {
        device::DeviceRegion{systick},
        device::DeviceRegion{nvic},
        device::DeviceRegion{scb},
        device::DeviceRegion{rcc},
        device::DeviceRegion{afio},
        device::DeviceRegion{exti},
        device::DeviceRegion{flash_interface},
        device::DeviceRegion{gpioa},
        device::DeviceRegion{gpiob},
        device::DeviceRegion{gpioc},
        device::DeviceRegion{usart1},
        device::DeviceRegion{usart2},
        device::DeviceRegion{tim2},
        device::DeviceRegion{tim3},
    };
    arch::arm::cortex_m3::CortexM3CPU cpu{&memory};
    std::vector<char> console;
    bool semihosting_enabled = false;
    RunResult status{.reason = StopReason::kHalted};
    uint64_t systick_remainder = 0;
    uint64_t tim2_remainder = 0;
    uint64_t tim3_remainder = 0;
    uint64_t systick_divisor = 1;
    uint64_t tim2_divisor = 1;
    uint64_t tim3_divisor = 1;
};

} // namespace micro_forge::machines
