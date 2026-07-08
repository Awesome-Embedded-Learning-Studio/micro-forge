// Board view — paints an STM32F103 chip with select pins wired to external
// LEDs / UART / SWD. This is the GUI's central stage: the LED visibly toggles
// as firmware drives GPIO — micro-forge's distinctive advantage over a
// real-hardware debugger, which can't show "the light is on" without staring
// at a physical board. Read-only output for now (LED reflects GPIO ODR);
// pin-level input injection is a later batch (A2).
//
// Pin map is fixed to the demo firmware + standard F103 assignments:
//   PA5  → LED1   (the gpio_blink demo toggles this pin)
//   PA0  → LED2
//   PA9  → USART1 TX     PA10 → USART1 RX
//   PA13 → SWDIO          PA14 → SWCLK
//   PC13 → board LED (LED3)
#pragma once

#include "introspection/introspection.hpp"

#include <array>
#include <cstdint>

#include <QWidget>

namespace micro_forge::gui::view {

class BoardView : public QWidget {
    Q_OBJECT
  public:
    explicit BoardView(QWidget* parent = nullptr);

    // Update rendered pin levels from the snapshot, then request an async repaint.
    void refresh(const introspection::IntrospectionSnapshot& snap);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    // GPIO output data registers per port A/B/C — bit i = pin i driven high.
    // Saved by refresh(), read by paintEvent() (Qt may repaint at any time,
    // independent of the tick loop).
    std::array<std::uint16_t, 3> odr_{};
};

} // namespace micro_forge::gui::view
