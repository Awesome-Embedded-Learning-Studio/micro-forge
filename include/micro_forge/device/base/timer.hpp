#pragma once

#include <cstdint>

namespace micro_forge::device {

/// @brief Basic counter and prescaler control independent of register layout.
class Timer {
public:
    virtual ~Timer() = default;

    virtual void enable(bool en) = 0;
    virtual void set_prescaler(uint32_t psc) = 0;
    virtual void set_auto_reload(uint32_t arr) = 0;
    [[nodiscard]] virtual uint32_t counter() const = 0;
    [[nodiscard]] virtual bool update_flag() const = 0;
    virtual void clear_update_flag() = 0;
};

} // namespace micro_forge::device
