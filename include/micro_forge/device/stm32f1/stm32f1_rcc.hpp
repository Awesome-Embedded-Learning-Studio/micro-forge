#pragma once

#include "micro_forge/device/base/clock_controller.hpp"
#include "micro_forge/device/base/device.hpp"
#include "micro_forge/device/base/weak_ptr.hpp"

#include <cstdint>

namespace micro_forge::device::stm32f1 {

/// @brief STM32F1 clock configuration and peripheral enable registers.
class Stm32f1Rcc : public device::Device, public device::ClockController {
public:
    void reset() override;

    Stm32f1Rcc() = default;

    // Device
    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    [[nodiscard]] std::string_view name() const noexcept override { return "RCC"; }

    // ClockController
    [[nodiscard]] bool is_clock_enabled(uint32_t peripheral_addr) const override;
    void enable_clock(uint32_t peripheral_addr) override;

    [[nodiscard]] WeakPtr<Stm32f1Rcc> get_weak() { return weak_factory_.get_weak_ptr(); }

private:
    void apply_cr_write(uint32_t data);

    // CR bit masks
    static constexpr uint32_t kHsiOn = 1U << 0U;
    static constexpr uint32_t kHsiRdy = 1U << 1U;
    static constexpr uint32_t kHseOn = 1U << 16U;
    static constexpr uint32_t kHseRdy = 1U << 17U;
    static constexpr uint32_t kPllOn = 1U << 24U;
    static constexpr uint32_t kPllRdy = 1U << 25U;

    // CFGR SW/SWS masks
    static constexpr uint32_t kSwMask = 0x3U;
    static constexpr uint32_t kSwShift = 0U;
    static constexpr uint32_t kSwsShift = 2U;

    uint32_t cr_ = 0x00000083U;
    uint32_t cfgr_ = 0x00000000U;
    uint32_t cir_ = 0x00000000U;
    uint32_t ahbenr_ = 0x00000000U;
    uint32_t apb2enr_ = 0x00000000U;
    uint32_t apb1enr_ = 0x00000000U;

    WeakPtrFactory<Stm32f1Rcc> weak_factory_{this};
};

} // namespace micro_forge::device::stm32f1
