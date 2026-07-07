#pragma once

#include "memory/bus.hpp"
#include "memory/flat_memory.hpp"

namespace micro_forge::chips::stm32f1 {

Expected<void> configure_memory(memory::Bus& bus, memory::FlatMemory& flash,
                                memory::FlatMemory& sram);

} // namespace micro_forge::chips::stm32f1
