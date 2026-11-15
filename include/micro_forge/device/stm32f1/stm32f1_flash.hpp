#pragma once

#include "micro_forge/device/base/device.hpp"
#include "micro_forge/device/base/weak_ptr.hpp"
#include <cstdint>

namespace micro_forge::device::stm32f1 {

/// @brief Flash interface registers only, without erase/program execution.
class Stm32f1Flash : public device::Device {
public:
    void reset() override;

    Stm32f1Flash() = default;

    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    [[nodiscard]] std::string_view name() const noexcept override { return "FLASH"; }

    [[nodiscard]] WeakPtr<Stm32f1Flash> get_weak() { return weak_factory_.get_weak_ptr(); }

private:
    uint32_t acr_ = 0x00000030U; // PRFTBE=1, LATENCY=1
    uint32_t keyr_ = 0x00000000U;
    uint32_t optkeyr_ = 0x00000000U;
    uint32_t sr_ = 0x00000000U; // Never busy
    uint32_t cr_ = 0x00000080U; // LOCK=1 after reset

    WeakPtrFactory<Stm32f1Flash> weak_factory_{this};
};

} // namespace micro_forge::device::stm32f1
