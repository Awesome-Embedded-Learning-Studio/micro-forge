#pragma once

#include "periph/device.hpp"
#include "util/weak_ptr/weak_ptr_factory.hpp"
#include <cstdint>

namespace micro_forge::chips::stm32f1 {

class Stm32f1Flash : public periph::Device {
  public:
    Stm32f1Flash() = default;

    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    std::string_view name() const noexcept override { return "FLASH"; }

    WeakPtr<Stm32f1Flash> GetWeak() { return weak_factory_.GetWeakPtr(); }

  private:
    uint32_t acr_ = 0x00000030; // PRFTBE=1, LATENCY=1
    uint32_t keyr_ = 0x00000000;
    uint32_t optkeyr_ = 0x00000000;
    uint32_t sr_ = 0x00000000; // Never busy
    uint32_t cr_ = 0x00000080; // LOCK=1 after reset

    WeakPtrFactory<Stm32f1Flash> weak_factory_{this};
};

} // namespace micro_forge::chips::stm32f1
