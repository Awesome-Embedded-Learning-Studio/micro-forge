// GPIO panel — A/B/C port ODR (hex) + 16 per-pin LED glyphs (pin0..pin15),
// plus a PA0 push-button for IDR input injection. Firmware that polls IDR
// (e.g. TAMCPP 2_button_control's HAL_GPIO_ReadPin) reads the injected level;
// the ODR display above still reflects what the firmware drives out.
#pragma once

#include "introspection/introspection.hpp"

#include <cstdint>

#include <QWidget>

class QLabel;
class QPushButton;

namespace micro_forge::gui::panels {

class GpioPanel : public QWidget {
    Q_OBJECT
  public:
    explicit GpioPanel(QWidget* parent = nullptr);

    void refresh(const introspection::IntrospectionSnapshot& snap);

  signals:
    // PA0 button toggled. Active-low wiring: checked (held) = drive IDR low,
    // released = drive IDR high — matching a pull-up button to GND.
    void injectGpio(char port, std::uint8_t pin, bool high);

  private:
    QLabel* label_;
    QPushButton* pa0_btn_;
};

} // namespace micro_forge::gui::panels
