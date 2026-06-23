#include "chips/stm32f1/peripheral_config.hpp"
#include "chips/stm32f1/stm32f103_soc.hpp"
#include "core/mem_literal.hpp"

namespace micro_forge::chips::stm32f1 {

using namespace micro_forge::literals;

Expected<void> configure_peripherals(memory::Bus& bus, Stm32f103Parts& parts) {
    auto map_checked = [&](addr_t base, addr_t size, auto weak) {
        return bus.map(memory::region(base, size, weak));
    };

    auto result = map_checked(0x4002'1000_addr, 0x400_addr,
                              parts.rcc.GetWeak());
    if (!result) {
        return result;
    }

    result = map_checked(0x4001'0000_addr, 0x400_addr,
                         parts.afio.GetWeak());
    if (!result) {
        return result;
    }

    result = map_checked(0x4002'2000_addr, 0x400_addr,
                         parts.flash_if.GetWeak());
    if (!result) {
        return result;
    }

    result = map_checked(0x4001'0800_addr, 0x400_addr,
                         parts.gpioa.GetWeak());
    if (!result) {
        return result;
    }

    result = map_checked(0x4001'0C00_addr, 0x400_addr,
                         parts.gpiob.GetWeak());
    if (!result) {
        return result;
    }

    result = map_checked(0x4001'1000_addr, 0x400_addr,
                         parts.gpioc.GetWeak());
    if (!result) {
        return result;
    }

    result = map_checked(0x4001'3800_addr, 0x400_addr,
                         parts.usart1.GetWeak());
    if (!result) {
        return result;
    }

    result = map_checked(0x4000'0000_addr, 0x400_addr,
                         parts.tim2.GetWeak());
    if (!result) {
        return result;
    }

    result = map_checked(0x4001'0400_addr, 0x400_addr,
                         parts.exti.GetWeak());
    if (!result) {
        return result;
    }

    return {};
}

} // namespace micro_forge::chips::stm32f1
