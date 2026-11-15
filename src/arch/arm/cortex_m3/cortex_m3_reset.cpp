#include "micro_forge/arch/arm/cortex_m3/cortex_m3_reset.hpp"
#include "micro_forge/arch/arm/cortex_m3/cortex_m3.hpp"
#include "micro_forge/arch/base/memory_access.hpp"

namespace micro_forge::arch::arm::cortex_m3 {

std::expected<void, std::string> cortex_m3_reset(CortexM3CPU& cpu, arch::MemoryAccess& bus,
                                                 uint32_t vector_table_base) {

    cpu.set_vector_table_base(vector_table_base);

    auto sp_result = bus.read(vector_table_base + 0, Width::kWord);
    if (!sp_result) {
        return std::unexpected("failed to read initial SP from vector table");
    }
    uint32_t sp = *sp_result;

    auto pc_result = bus.read(vector_table_base + 4, Width::kWord);
    if (!pc_result) {
        return std::unexpected("failed to read reset handler from vector table");
    }
    uint32_t pc = *pc_result;

    auto reset_r = cpu.reset();
    if (!reset_r) {
        return std::unexpected("CPU reset failed");
    }

    if (!cpu.set_register_value(13, sp)) {
        return std::unexpected("failed to set SP");
    }

    // XPSR.T set by reset() is the real mode indicator; bit 0 is metadata.
    pc |= 1;
    // Strip Thumb bit before writing to PC register.
    // Thumb mode is indicated by XPSR.T (set by cpu.reset()).
    uint32_t exec_addr = pc & ~1u;
    if (!cpu.set_pc(exec_addr)) {
        return std::unexpected("failed to set PC");
    }

    if (!cpu.set_register_value(14, 0xFFFFFFFF)) {
        return std::unexpected("failed to set LR");
    }

    return {};
}

} // namespace micro_forge::arch::arm::cortex_m3
