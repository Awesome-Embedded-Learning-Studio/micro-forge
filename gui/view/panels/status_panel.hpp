// Status / masks panel — xPSR, PRIMASK, BASEPRI, FAULTMASK, CONTROL, MSP, PSP.
// These are the "why did the CPU do that" registers: interrupt masking,
// stack selection, condition flags. Watching them change while stepping is
// the teaching moment for ARMv7-M exception semantics.
#pragma once

#include "introspection/introspection.hpp"

#include <QWidget>

class QTableWidget;

namespace micro_forge::gui::panels {

class StatusPanel : public QWidget {
    Q_OBJECT

  public:
    explicit StatusPanel(QWidget* parent = nullptr);

    void refresh(const introspection::IntrospectionSnapshot& snap);

  private:
    QTableWidget* table_;
};

} // namespace micro_forge::gui::panels
