#pragma once

#include <cstdint>
#include <functional>

namespace micro_forge::device {

/// @brief Byte output interface for serial peripherals.
class SerialPort {
public:
    virtual ~SerialPort() = default;

    virtual void send_byte(uint8_t byte) = 0;
    [[nodiscard]] virtual bool can_send() const = 0;

    using OutputCallback = std::function<void(uint8_t)>;
    virtual void set_output(OutputCallback cb) = 0;
};

} // namespace micro_forge::device
