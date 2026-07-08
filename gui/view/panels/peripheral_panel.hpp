// Peripheral status panel — SysTick / NVIC / SCB summary, the interrupt-system
// "instrument cluster": tick timer state, pending IRQ, priority grouping.
#pragma once

#include "introspection/introspection.hpp"

#include <QWidget>

class QLabel;

namespace micro_forge::gui::panels {

class PeripheralPanel : public QWidget {
    Q_OBJECT

  public:
    explicit PeripheralPanel(QWidget* parent = nullptr);

    void refresh(const introspection::IntrospectionSnapshot& snap);

  private:
    QLabel* label_;
};

} // namespace micro_forge::gui::panels
