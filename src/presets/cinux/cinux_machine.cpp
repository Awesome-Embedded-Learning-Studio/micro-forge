/**
 * @file machine_config.cpp
 * @brief The Cinux preset (the historical hard-coded layout, as data).
 * @version 0.1.0
 */

#include "micro_forge/presets/cinux_machine.hpp"

namespace micro_forge {

MachineConfig cinux_machine_config(uint64_t ram_size, std::optional<uintptr_t> window) {
    MachineConfig config;
    config.ram_size = ram_size;
    config.window = window;
    config.ports = {
        {.base = 0x3F8, .size = 8, .device = MachineDevice::kSerial},
        {.base = 0x40, .size = 4, .device = MachineDevice::kPit},
        {.base = 0x61, .size = 1, .device = MachineDevice::kBoard},
        {.base = 0x70, .size = 2, .device = MachineDevice::kRtc},
        {.base = 0x80, .size = 1, .device = MachineDevice::kPost},
        {.base = 0x510, .size = 2, .device = MachineDevice::kFwCfg},
        {.base = 0xCF8, .size = 8, .device = MachineDevice::kPci},
        {.base = 0x60, .size = 1, .device = MachineDevice::kKbdData},
        {.base = 0x64, .size = 1, .device = MachineDevice::kKbdStatus},
        {.base = 0x20, .size = 2, .device = MachineDevice::kPicMaster},
        {.base = 0xA0, .size = 2, .device = MachineDevice::kPicSlave},
    };
    config.windows = {
        {.base = 0xFE000000ULL, .size = 0x2000, .device = MachineDevice::kAhci},
        {.base = 0xFEE00000ULL, .size = 0x1000, .device = MachineDevice::kLapic},
        {.base = 0xFEC00000ULL, .size = 0x1000, .device = MachineDevice::kIoApic},
    };
    return config;
}

} // namespace micro_forge
