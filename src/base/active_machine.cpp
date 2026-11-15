/**
 * @file active_machine.cpp
 * @brief Machine description -> MachineConfig assembly.
 * @version 0.1.0
 */

#include "micro_forge/base/active_machine.hpp"

#include "micro_forge/generated/machine_config_generated.hpp"

namespace micro_forge {

MachineConfig active_machine_config(uint64_t ram_size, std::optional<uintptr_t> window) {
    MachineConfig config;
    config.ram_size = ram_size != 0 ? ram_size : generated::kDefaultRamSize;
    config.window = window;
    config.ports.assign(generated::kPortWindows.begin(), generated::kPortWindows.end());
    config.windows.assign(generated::kMemoryWindows.begin(), generated::kMemoryWindows.end());
    config.ahci_vendor = generated::kAhciVendor;
    config.ahci_device = generated::kAhciDevice;
    config.ahci_class = generated::kAhciClass;
    config.ahci_irq = generated::kAhciIrq;
    config.jit = generated::kJitEnabled;
    return config;
}

} // namespace micro_forge
