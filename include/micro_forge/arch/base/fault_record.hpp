#pragma once

#include "micro_forge/arch/base/cpu.hpp"
#include "micro_forge/device/base/types.hpp"

#include <cstdint>
#include <optional>

namespace micro_forge::arch::cpu {

using device::addr_t;
using device::BusError;
using device::data_t;
using device::Width;

struct FaultRecord {
    addr_t pc = 0;
    addr_t lr = 0;
    addr_t sp = 0;
    data_t xpsr = 0;
    uint16_t opcode16 = 0;
    uint16_t opcode16_2 = 0;
    bool is_32bit = false;
    CPU::CPUError kind{};
    std::optional<BusError> bus_error;
    std::optional<addr_t> access_addr;
    std::optional<Width> access_width;
};

} // namespace micro_forge::arch::cpu
