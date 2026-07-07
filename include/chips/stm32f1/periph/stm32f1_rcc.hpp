#pragma once

#include "periph/clock_controller.hpp"
#include "periph/device.hpp"
#include "util/weak_ptr/weak_ptr_factory.hpp"

#include <cstdint>

namespace micro_forge::chips::stm32f1 {

class Stm32f1Rcc : public periph::Device, public periph::ClockController {
  public:
    Stm32f1Rcc() = default;

    // Device
    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    std::string_view name() const noexcept override { return "RCC"; }

    // ClockController
    bool is_clock_enabled(uint32_t peripheral_addr) const override;
    void enable_clock(uint32_t peripheral_addr) override;

    WeakPtr<Stm32f1Rcc> GetWeak() { return weak_factory_.GetWeakPtr(); }

  private:
    void apply_cr_write(uint32_t data);

    // CR bit masks
    static constexpr uint32_t kHsiOn = 1u << 0;
    static constexpr uint32_t kHsiRdy = 1u << 1;
    static constexpr uint32_t kHseOn = 1u << 16;
    static constexpr uint32_t kHseRdy = 1u << 17;
    static constexpr uint32_t kPllOn = 1u << 24;
    static constexpr uint32_t kPllRdy = 1u << 25;

    // CFGR SW/SWS masks
    static constexpr uint32_t kSwMask = 0x3u;
    static constexpr uint32_t kSwShift = 0;
    static constexpr uint32_t kSwsShift = 2;

    uint32_t cr_ = 0x00000083;
    uint32_t cfgr_ = 0x00000000;
    uint32_t cir_ = 0x00000000;
    uint32_t ahbenr_ = 0x00000000;
    uint32_t apb2enr_ = 0x00000000;
    uint32_t apb1enr_ = 0x00000000;

    WeakPtrFactory<Stm32f1Rcc> weak_factory_{this};
};

} // namespace micro_forge::chips::stm32f1
