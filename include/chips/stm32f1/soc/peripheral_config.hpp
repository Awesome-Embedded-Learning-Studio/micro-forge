#pragma once

#include "memory/bus.hpp"

namespace micro_forge::chips::stm32f1 {

struct Stm32f103Parts;

Expected<void> configure_peripherals(memory::Bus& bus, Stm32f103Parts& parts);

} // namespace micro_forge::chips::stm32f1
