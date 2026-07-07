#include "chips/stm32f1/soc/interrupt_config.hpp"
#include "core/mem_literal.hpp"

namespace micro_forge::chips::stm32f1 {

using namespace micro_forge::literals;

Expected<void> configure_interrupt_devices(memory::Bus& bus,
                                           periph::NvicPeripheral& nvic,
                                           periph::SysTickPeripheral& systick,
                                           periph::ScbPeripheral& scb) {
    // SysTick: 0xE000E010, 16 bytes
    auto result =
        bus.map(memory::region(0xE000'E010_addr, 0x10_addr, systick.GetWeak()));
    if (!result) {
        return result;
    }

    // NVIC: 0xE000E100, 3KB (covers ISER through IP registers)
    result =
        bus.map(memory::region(0xE000'E100_addr, 0xC00_addr, nvic.GetWeak()));
    if (!result) {
        return result;
    }

    // SCB: 0xE000ED00, 64 bytes (CPUID through SHCSR)
    result =
        bus.map(memory::region(0xE000'ED00_addr, 0x40_addr, scb.GetWeak()));
    if (!result) {
        return result;
    }

    return {};
}

} // namespace micro_forge::chips::stm32f1
