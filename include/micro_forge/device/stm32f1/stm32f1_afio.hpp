#pragma once

#include "micro_forge/device/base/device.hpp"
#include "micro_forge/device/base/weak_ptr.hpp"
#include <cstdint>

namespace micro_forge::device::stm32f1 {

/// @brief STM32F1 alternate-function and EXTI routing registers.
class Stm32f1Afio : public device::Device {
public:
    void reset() override;

    Stm32f1Afio() = default;

    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    [[nodiscard]] std::string_view name() const noexcept override { return "AFIO"; }

    // EXTI line N → owning GPIO port index (0=PA,1=PB,2=PC,3=PD,4=PE),
    // decoded from EXTICR. Consumed by Stm32f1Exti to route GPIO edges.
    [[nodiscard]] uint8_t exti_line_port(uint8_t line) const;

    [[nodiscard]] WeakPtr<Stm32f1Afio> get_weak() { return weak_factory_.get_weak_ptr(); }

private:
    uint32_t evcr_ = 0x00000000U;
    uint32_t mapr_ = 0x00000000U;
    uint32_t exticr_[4U] = {};
    uint32_t mapr2_ = 0x00000000U;

    WeakPtrFactory<Stm32f1Afio> weak_factory_{this};
};

} // namespace micro_forge::device::stm32f1
