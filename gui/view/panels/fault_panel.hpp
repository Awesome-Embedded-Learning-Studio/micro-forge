// Fault detail panel — when a fault fires, show its kind, the faulting
// instruction, and (if it was a bus access) the address + BusError. Shows
// "— none —" while no fault is present.
#pragma once

#include "introspection/introspection.hpp"

#include <QWidget>

class QLabel;

namespace micro_forge::gui::panels {

class FaultPanel : public QWidget {
    Q_OBJECT

  public:
    explicit FaultPanel(QWidget* parent = nullptr);

    void refresh(const introspection::IntrospectionSnapshot& snap);

  private:
    QLabel* label_;
};

} // namespace micro_forge::gui::panels
