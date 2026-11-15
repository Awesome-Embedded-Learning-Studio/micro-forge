#pragma once

#include <cstdint>
#include <functional>

namespace micro_forge::device {

enum class PinMode : uint8_t { kInput, kOutput, kAlternate, kAnalog };
enum class PinPull : uint8_t { kNone, kPullUp, kPullDown };
enum class PinSpeed : uint8_t { kLow, kMedium, kHigh };

/// @brief Pin-level configuration and observable input/output interface.
class Gpio {
public:
    virtual ~Gpio() = default;

    virtual void set_pin(uint8_t pin, bool high) = 0;
    [[nodiscard]] virtual bool get_pin(uint8_t pin) const = 0;

    virtual void configure_pin(uint8_t pin, PinMode mode, PinPull pull = PinPull::kNone,
                               PinSpeed speed = PinSpeed::kLow) = 0;

    virtual void simulate_input(uint8_t pin, bool high) = 0;

    using PinChangeCallback = std::function<void(uint8_t pin, bool high)>;
    virtual void set_pin_change_callback(PinChangeCallback cb) = 0;
};

} // namespace micro_forge::device
