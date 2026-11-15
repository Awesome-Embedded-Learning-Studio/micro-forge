#pragma once
#include "micro_forge/device/base/types.hpp"
#include <cstdint>
#include <string_view>

namespace micro_forge::arch {
namespace arm::cortex_m3 {
using device::data_t;
/* xpsr_ usage */
static constexpr data_t kPsrN = 1u << 31;
static constexpr data_t kPsrZ = 1u << 30;
static constexpr data_t kPsrC = 1u << 29;
static constexpr data_t kPsrV = 1u << 28;
static constexpr data_t kPsrQ = 1u << 27;
static constexpr data_t kPsrT = 1u << 24;
static constexpr uint16_t kRegisterCount = 16;

static constexpr std::string_view kRegisterNames[] = {"R0",  "R1", "R2", "R3", "R4",  "R5",
                                                      "R6",  "R7", "R8", "R9", "R10", "R11",
                                                      "R12", "SP", "LR", "PC"};
} // namespace arm::cortex_m3
} // namespace micro_forge::arch
