// STM32F103 board widget — paints the chip + pins wired to external LEDs /
// UART / SWD. The LEDs reflect GPIO ODR bits in real time: micro-forge's
// distinctive advantage over a real-hardware debugger (which can't show "the
// light is on" without staring at a physical board). Read-only output;
// pin-level input injection is handled in the gpio_panel (A2 buttons).
//
// Port A is drawn as a row of 8 LEDs (PA0..PA7) so ANY demo firmware driving a
// port-A pin shows up — gpio_blink uses PA5, the F103 CubeMX example uses PA1.
// PC13 (board LED) + USART1 TX/RX + SWD are also drawn.
#pragma once

#include "introspection/introspection.hpp"

#include <array>
#include <cstdint>

#include <QWidget>

namespace micro_forge::gui::view {

class Stm32BoardWidget : public QWidget {
    Q_OBJECT
  public:
    explicit Stm32BoardWidget(QWidget* parent = nullptr);

    // Update rendered pin levels from the snapshot, then request an async repaint.
    void refresh(const introspection::IntrospectionSnapshot& snap);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    // GPIO ODR per port A/B/C — bit i = pin i driven high. Saved by refresh(),
    // read by paintEvent() (Qt may repaint at any time, independent of ticks).
    std::array<std::uint16_t, 3> odr_{};
};

} // namespace micro_forge::gui::view
