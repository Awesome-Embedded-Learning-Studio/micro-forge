/**
 * @file machine_config.hpp
 * @brief Source-compatible aliases for the x86 PC layout description.
 * @version 0.1.0
 */

#pragma once

#include "micro_forge/machines/x86_pc_config.hpp"

namespace micro_forge {

/// @brief Historical device catalogue for the x86 PC only.
using MachineDevice = machines::X86PcDevice;
/// @brief Historical name for a PC port window.
using PortWindow = machines::X86PcPortWindow;
/// @brief Historical name for a PC device memory window.
using MemoryWindow = machines::X86PcMemoryWindow;
/// @brief Historical name for PC configuration; other boards define their own.
using MachineConfig = machines::X86PcConfig;

} // namespace micro_forge
