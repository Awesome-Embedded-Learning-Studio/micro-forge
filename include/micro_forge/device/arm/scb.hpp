#pragma once

#include "micro_forge/device/base/device.hpp"
#include "micro_forge/device/base/weak_ptr.hpp"
#include <cstdint>
#include <functional>

namespace micro_forge::device::arm {

/// @brief Cortex-M3 system control registers and CPU configuration callbacks.
class ScbPeripheral : public Device {
public:
    void reset() override;

    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    [[nodiscard]] std::string_view name() const noexcept override { return "SCB"; }

    [[nodiscard]] WeakPtr<ScbPeripheral> get_weak() { return weak_factory_.get_weak_ptr(); }

    void set_vtor_callback(std::function<void(uint32_t)> cb) { vtor_cb_ = std::move(cb); }

    void set_prigroup_callback(std::function<void(uint8_t)> cb) { prigroup_cb_ = std::move(cb); }

    // Priority of system exception `exc_num` (4..15), stored in SHPR1/2/3.
    // Returns 0xFF for exceptions without a configurable priority.
    // Note: HardFault(3), NMI(2), reset(1) have fixed priority and are not
    // looked up here.
    [[nodiscard]] uint8_t system_exception_priority(uint8_t exc_num) const noexcept {
        if (exc_num >= 4U && exc_num <= 15U) {
            return shp_[exc_num - 4U];
        }
        return 0xFFU;
    }

    // Read-only accessors backing the SCB panel of the introspection snapshot
    // (milestone 04 GUI). PRIGROUP is AIRCR bits [10:8].
    [[nodiscard]] uint32_t icsr() const noexcept { return icsr_; }
    [[nodiscard]] uint32_t vtor() const noexcept { return vtor_; }
    [[nodiscard]] uint32_t aircr() const noexcept { return aircr_; }
    [[nodiscard]] uint8_t prigroup() const noexcept {
        return static_cast<uint8_t>((aircr_ >> 8U) & 0x7U);
    }

private:
    uint32_t cpuid_ = 0x412FC230U; // Cortex-M3 r2p0
    uint32_t icsr_ = 0U;
    uint32_t vtor_ = 0U;
    uint32_t aircr_ = 0xFA050000U;
    uint32_t scr_ = 0U;
    uint32_t ccr_ = 0x00000200U; // STKALIGN
    uint8_t shp_[12U] = {};
    uint32_t shcsr_ = 0U;

    std::function<void(uint32_t)> vtor_cb_;
    std::function<void(uint8_t)> prigroup_cb_;

    WeakPtrFactory<ScbPeripheral> weak_factory_{this};
};

} // namespace micro_forge::device::arm
