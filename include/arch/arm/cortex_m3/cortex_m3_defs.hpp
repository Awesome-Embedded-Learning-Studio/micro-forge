#pragma once
#include "autogen/arch_details.hpp"
#include <cstdint>
#include <string_view>

namespace micro_forge::cpu {
namespace arm::cortex_m3 {
/* xpsr_ usage */
static constexpr data_t PSR_N = 1u << 31;
static constexpr data_t PSR_Z = 1u << 30;
static constexpr data_t PSR_C = 1u << 29;
static constexpr data_t PSR_V = 1u << 28;
static constexpr data_t PSR_Q = 1u << 27;
static constexpr data_t PSR_T = 1u << 24;
static constexpr uint16_t REGCNT = 16;

static constexpr std::string_view names[] = {
    "R0", "R1", "R2",  "R3",  "R4",  "R5", "R6", "R7",
    "R8", "R9", "R10", "R11", "R12", "SP", "LR", "PC"};
} // namespace arm::cortex_m3
} // namespace micro_forge::cpu