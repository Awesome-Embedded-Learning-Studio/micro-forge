#pragma once

#include <cstdint>

namespace micro_forge::hooks {

// Every event carries a cycle stamp from the simulator's free-running tick
// counter, so consumers can order and time events across kinds (a UART byte
// that landed between two GPIO edges, etc.). Zero until a cycle source is
// wired in (see EventBus / Gpio::set_cycle_source).
struct EventHeader {
    uint64_t cycle = 0;
};

// A GPIO output pin changed level. Emitted only on a real edge (low→high or
// high→low), not on writes that leave ODR unchanged.
struct GpioEdge : EventHeader {
    char port;   // 'A'..'E'
    uint8_t pin; // 0..15
    bool rising; // true = low→high
};

// One byte transmitted on a USART. (Wiring TODO — UART already has
// OutputCallback; the bus unifies it under the same subscription model.)
struct UartByte : EventHeader {
    uint8_t unit; // 1, 2, 3
    uint8_t byte;
};

// Extend freely: ClockChange, Fault, ExceptionEntry/Return, custom user
// events. Anything you want to "grab" becomes a struct + a Signal<E>.

} // namespace micro_forge::hooks
