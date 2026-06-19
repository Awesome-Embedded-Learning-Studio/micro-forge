#pragma once

#include "hooks/events.hpp"
#include "hooks/signal.hpp"

#include <cstdint>
#include <functional>

namespace micro_forge::hooks {

// One home for every observable signal. The platform stamps each event with
// a cycle via set_cycle_source(); peripherals call now() when they emit so
// they don't each need to know about the clock.
//
// Today only gpio is wired (peripheral-owned Signal); uart and friends are
// declared to show the shape — bringing them under the bus is a matter of
// pointing their existing callbacks at bus.uart.emit().
class EventBus {
  public:
    using CycleSource = std::function<uint64_t()>;

    Signal<GpioEdge> gpio;
    Signal<UartByte> uart;

    void set_cycle_source(CycleSource src) { cycle_ = std::move(src); }
    uint64_t now() const { return cycle_ ? cycle_() : 0; }

  private:
    CycleSource cycle_;
};

} // namespace micro_forge::hooks
