/**
 * @file memory_access.hpp
 * @brief MCU width/error adaptation over the shared kernel bus, without a second map.
 * @version 0.1.0
 */
#pragma once

#include "micro_forge/device/base/types.hpp"
#include "micro_forge/kernel/bus/bus.hpp"

namespace micro_forge::arch {

using device::addr_t;
using device::BusError;
using device::data_t;
using device::Expected;
using device::Width;

/** @brief Non-owning instruction-side view of the machine's only address router. */
class MemoryAccess {
public:
    explicit MemoryAccess(kernel::Bus& bus) : bus_(bus) {}

    /**
     * @brief Reads using MCU access width and error classification.
     * @param[in] address Absolute 32-bit guest address.
     * @param[in] width Guest access width.
     * @return Value, or an error from the shared bus.
     */
    [[nodiscard]] Expected<data_t> read(addr_t address, Width width);

    /**
     * @brief Writes using MCU access width and error classification.
     * @param[in] address Absolute 32-bit guest address.
     * @param[in] data Guest value.
     * @param[in] width Guest access width.
     * @return Success or the bus error, never an implicit successful fallback.
     */
    [[nodiscard]] Expected<void> write(addr_t address, data_t data, Width width);

private:
    kernel::Bus& bus_;
};

} // namespace micro_forge::arch
