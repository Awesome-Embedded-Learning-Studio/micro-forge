#include "chips/stm32f1/soc/memory_bus.hpp"
#include "core/mem_literal.hpp"

namespace micro_forge::chips::stm32f1 {

using namespace micro_forge::literals;

Expected<void> configure_memory(memory::Bus& bus, memory::FlatMemory& flash,
                                memory::FlatMemory& sram) {
    // Boot alias: same flash instance at 0x00000000
    auto result =
        bus.map(memory::region(0x0000'0000_addr, 128_kb, flash.GetWeak()));
    if (!result.has_value()) {
        return result;
    }

    // Flash: 128KB @ 0x08000000
    result = bus.map(memory::region(0x0800'0000_addr, 128_kb, flash.GetWeak()));
    if (!result.has_value()) {
        return result;
    }

    // SRAM: 20KB @ 0x20000000
    result = bus.map(memory::region(0x2000'0000_addr, 20_kb, sram.GetWeak()));
    if (!result.has_value()) {
        return result;
    }

    return {};
}

} // namespace micro_forge::chips::stm32f1
