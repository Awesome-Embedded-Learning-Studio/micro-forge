// STM32F103 board widget — paints the chip + pins wired to UART / SWD / PC13,
// and hosts a LedPanel child for the Port-A LED row (PA0..PA7). The LEDs reflect
// GPIO ODR bits in real time: micro-forge's distinctive advantage over a
// real-hardware debugger (which can't show "the light is on" without staring at
// a physical board). Read-only output; pin-level input injection is handled in
// the gpio_panel (A2 buttons).
//
// Port A (PA0..PA7) is shown via a quark::LedPanel child — any demo firmware
// driving a port-A pin lights up (gpio_blink uses PA5, the F103 CubeMX example
// uses PA1). PA9/PA10 UART markers + SWD + PC13 are still custom-painted here.
#pragma once

#include "introspection/introspection.hpp"

#include <array>
#include <cstdint>

#include <QWidget>

namespace quark { class LedPanel; }  // hosted child for PA0..PA7

namespace micro_forge::gui::view {

class Stm32BoardWidget : public QWidget {
    Q_OBJECT
  public:
    explicit Stm32BoardWidget(QWidget* parent = nullptr);

    // Update rendered pin levels from the snapshot, then request an async repaint.
    void refresh(const introspection::IntrospectionSnapshot& snap);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    // GPIO ODR per port A/B/C — bit i = pin i driven high. Saved by refresh(),
    // read by paintEvent() (Qt may repaint at any time, independent of ticks).
    std::array<std::uint16_t, 3> odr_{};

    // Port-A LED row (PA0..PA7 + ODR hex), driven from odr_[0].
    quark::LedPanel* ledPanel_{};
};

} // namespace micro_forge::gui::view
