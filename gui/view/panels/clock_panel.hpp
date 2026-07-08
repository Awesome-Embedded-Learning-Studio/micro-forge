// Clock tree panel — renders the SYSCLK → HCLK → APB1/APB2 frequency tree.
// STM32's clock tree is one of its hardest concepts; surfacing it live is a
// teaching win (B1). Read-only; RCC frequency changes (firmware writing RCC
// prescalers / PLL) appear here on the next snapshot.
#pragma once

#include "introspection/introspection.hpp"

#include <QWidget>

class QLabel;

namespace micro_forge::gui::panels {

class ClockPanel : public QWidget {
    Q_OBJECT
  public:
    explicit ClockPanel(QWidget* parent = nullptr);

    void refresh(const introspection::IntrospectionSnapshot& snap);

  private:
    QLabel* label_;
};

} // namespace micro_forge::gui::panels
