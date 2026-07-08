// GPIO panel — A/B/C port ODR (hex) + 16 per-pin LED glyphs, plus push
// buttons that inject a pin level into the simulated GPIO input (A2). Buttons
// toggle their cached level each click and only emit a signal; MainWindow
// forwards it to model::Session.
#pragma once

#include "introspection/introspection.hpp"

#include <QWidget>

class QLabel;

namespace micro_forge::gui::panels {

class GpioPanel : public QWidget {
    Q_OBJECT
  public:
    explicit GpioPanel(QWidget* parent = nullptr);

    void refresh(const introspection::IntrospectionSnapshot& snap);

  Q_SIGNALS:
    // User drove a pin level via an on-screen button (A2 input injection).
    void gpioInputRequested(char port, int pin, bool high);

  private:
    QLabel* label_;
    // Toggle state for the input-injection buttons — UI affordance only,
    // tracking what the on-screen button currently shows (not sim state).
    bool pa0_high_ = false;
    bool pc13_high_ = false;
};

} // namespace micro_forge::gui::panels
