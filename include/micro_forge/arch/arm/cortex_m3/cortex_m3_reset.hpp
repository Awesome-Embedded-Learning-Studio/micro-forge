#pragma once

#include <cstdint>
#include <expected>
#include <string>

namespace micro_forge {

namespace arch {
class MemoryAccess;
}

namespace arch::arm::cortex_m3 {

class CortexM3CPU;

std::expected<void, std::string> cortex_m3_reset(CortexM3CPU& cpu, arch::MemoryAccess& bus,
                                                 uint32_t vector_table_base = 0x00000000);

} // namespace arch::arm::cortex_m3
} // namespace micro_forge
